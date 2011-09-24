/*
 * Copyright 2010 Maarten Lankhorst for CodeWeavers
 * Copyright 2011 Andrew Eikum for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define NONAMELESSUNION
#define COBJMACROS
#include "config.h"

#include <stdarg.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "wine/debug.h"
#include "wine/unicode.h"
#include "wine/list.h"

#include "ole2.h"
#include "mmdeviceapi.h"
#include "devpkey.h"
#include "dshow.h"
#include "dsound.h"

#include "initguid.h"
#include "endpointvolume.h"
#include "audioclient.h"
#include "audiopolicy.h"

#include <alsa/asoundlib.h>

WINE_DEFAULT_DEBUG_CHANNEL(alsa);
WINE_DECLARE_DEBUG_CHANNEL(winediag);

#define NULL_PTR_ERR MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, RPC_X_NULL_REF_POINTER)

static const REFERENCE_TIME DefaultPeriod = 100000;
static const REFERENCE_TIME MinimumPeriod = 50000;

struct ACImpl;
typedef struct ACImpl ACImpl;

typedef struct _AudioSession {
    GUID guid;
    struct list clients;

    IMMDevice *device;

    float master_vol;
    UINT32 channel_count;
    float *channel_vols;
    BOOL mute;

    CRITICAL_SECTION lock;

    struct list entry;
} AudioSession;

typedef struct _AudioSessionWrapper {
    IAudioSessionControl2 IAudioSessionControl2_iface;
    IChannelAudioVolume IChannelAudioVolume_iface;
    ISimpleAudioVolume ISimpleAudioVolume_iface;

    LONG ref;

    ACImpl *client;
    AudioSession *session;
} AudioSessionWrapper;

struct ACImpl {
    IAudioClient IAudioClient_iface;
    IAudioRenderClient IAudioRenderClient_iface;
    IAudioCaptureClient IAudioCaptureClient_iface;
    IAudioClock IAudioClock_iface;
    IAudioClock2 IAudioClock2_iface;
    IAudioStreamVolume IAudioStreamVolume_iface;

    LONG ref;

    snd_pcm_t *pcm_handle;
    snd_pcm_uframes_t alsa_bufsize_frames, alsa_period_frames;
    snd_pcm_hw_params_t *hw_params; /* does not hold state between calls */
    snd_pcm_format_t alsa_format;

    IMMDevice *parent;

    EDataFlow dataflow;
    WAVEFORMATEX *fmt;
    DWORD flags;
    AUDCLNT_SHAREMODE share;
    HANDLE event;
    float *vols;

    BOOL initted, started;
    REFERENCE_TIME mmdev_period_rt;
    UINT64 written_frames, last_pos_frames;
    UINT32 bufsize_frames, held_frames, tmp_buffer_frames, mmdev_period_frames;
    UINT32 lcl_offs_frames; /* offs into local_buffer where valid data starts */

    HANDLE timer;
    BYTE *local_buffer, *tmp_buffer;
    int buf_state;
    long getbuf_last; /* <0 when using tmp_buffer */

    CRITICAL_SECTION lock;

    AudioSession *session;
    AudioSessionWrapper *session_wrapper;

    struct list entry;
};

enum BufferStates {
    NOT_LOCKED = 0,
    LOCKED_NORMAL, /* public buffer piece is from local_buffer */
    LOCKED_WRAPPED /* public buffer piece is wrapped around, in tmp_buffer */
};

typedef struct _SessionMgr {
    IAudioSessionManager2 IAudioSessionManager2_iface;

    LONG ref;

    IMMDevice *device;
} SessionMgr;

static HANDLE g_timer_q;

static CRITICAL_SECTION g_sessions_lock;
static CRITICAL_SECTION_DEBUG g_sessions_lock_debug =
{
    0, 0, &g_sessions_lock,
    { &g_sessions_lock_debug.ProcessLocksList, &g_sessions_lock_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": g_sessions_lock") }
};
static CRITICAL_SECTION g_sessions_lock = { &g_sessions_lock_debug, -1, 0, 0, 0, 0 };
static struct list g_sessions = LIST_INIT(g_sessions);

static const WCHAR defaultW[] = {'d','e','f','a','u','l','t',0};
static const char defname[] = "default";

static const IAudioClientVtbl AudioClient_Vtbl;
static const IAudioRenderClientVtbl AudioRenderClient_Vtbl;
static const IAudioCaptureClientVtbl AudioCaptureClient_Vtbl;
static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl;
static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl;
static const IAudioClockVtbl AudioClock_Vtbl;
static const IAudioClock2Vtbl AudioClock2_Vtbl;
static const IAudioStreamVolumeVtbl AudioStreamVolume_Vtbl;
static const IChannelAudioVolumeVtbl ChannelAudioVolume_Vtbl;
static const IAudioSessionManager2Vtbl AudioSessionManager2_Vtbl;

static AudioSessionWrapper *AudioSessionWrapper_Create(ACImpl *client);

static inline ACImpl *impl_from_IAudioClient(IAudioClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClient_iface);
}

static inline ACImpl *impl_from_IAudioRenderClient(IAudioRenderClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioRenderClient_iface);
}

static inline ACImpl *impl_from_IAudioCaptureClient(IAudioCaptureClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioCaptureClient_iface);
}

static inline AudioSessionWrapper *impl_from_IAudioSessionControl2(IAudioSessionControl2 *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, IAudioSessionControl2_iface);
}

static inline AudioSessionWrapper *impl_from_ISimpleAudioVolume(ISimpleAudioVolume *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, ISimpleAudioVolume_iface);
}

static inline AudioSessionWrapper *impl_from_IChannelAudioVolume(IChannelAudioVolume *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, IChannelAudioVolume_iface);
}

static inline ACImpl *impl_from_IAudioClock(IAudioClock *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock_iface);
}

static inline ACImpl *impl_from_IAudioClock2(IAudioClock2 *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock2_iface);
}

static inline ACImpl *impl_from_IAudioStreamVolume(IAudioStreamVolume *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioStreamVolume_iface);
}

static inline SessionMgr *impl_from_IAudioSessionManager2(IAudioSessionManager2 *iface)
{
    return CONTAINING_RECORD(iface, SessionMgr, IAudioSessionManager2_iface);
}

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, void *reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_timer_q = CreateTimerQueue();
        if(!g_timer_q)
            return FALSE;
        break;

    case DLL_PROCESS_DETACH:
        DeleteCriticalSection(&g_sessions_lock);
        break;
    }
    return TRUE;
}

/* From <dlls/mmdevapi/mmdevapi.h> */
enum DriverPriority {
    Priority_Unavailable = 0,
    Priority_Low,
    Priority_Neutral,
    Priority_Preferred
};

int WINAPI AUDDRV_GetPriority(void)
{
    return Priority_Neutral;
}

static BOOL alsa_try_open(const char *devnode, snd_pcm_stream_t stream)
{
    snd_pcm_t *handle;
    int err;

    if((err = snd_pcm_open(&handle, devnode, stream, SND_PCM_NONBLOCK)) < 0){
        WARN("The device \"%s\" failed to open: %d (%s).\n",
                devnode, err, snd_strerror(err));
        return FALSE;
    }

    snd_pcm_close(handle);
    return TRUE;
}

static HRESULT alsa_get_card_devices(snd_pcm_stream_t stream, WCHAR **ids, char **keys,
        UINT *num, snd_ctl_t *ctl, int card, const WCHAR *cardnameW)
{
    static const WCHAR dashW[] = {' ','-',' ',0};
    int err, device;
    snd_pcm_info_t *info;

    info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, snd_pcm_info_sizeof());
    if(!info)
        return E_OUTOFMEMORY;

    snd_pcm_info_set_subdevice(info, 0);
    snd_pcm_info_set_stream(info, stream);

    device = -1;
    for(err = snd_ctl_pcm_next_device(ctl, &device); device != -1 && err >= 0;
            err = snd_ctl_pcm_next_device(ctl, &device)){
        const char *devname;
        char devnode[32];

        snd_pcm_info_set_device(info, device);

        if((err = snd_ctl_pcm_info(ctl, info)) < 0){
            if(err == -ENOENT)
                /* This device doesn't have the right stream direction */
                continue;

            WARN("Failed to get info for card %d, device %d: %d (%s)\n",
                    card, device, err, snd_strerror(err));
            continue;
        }

        sprintf(devnode, "plughw:%d,%d", card, device);
        if(!alsa_try_open(devnode, stream))
            continue;

        if(ids && keys){
            DWORD len, cardlen;

            devname = snd_pcm_info_get_name(info);
            if(!devname){
                WARN("Unable to get device name for card %d, device %d\n", card,
                        device);
                continue;
            }

            cardlen = lstrlenW(cardnameW);
            len = MultiByteToWideChar(CP_UNIXCP, 0, devname, -1, NULL, 0);
            len += lstrlenW(dashW);
            len += cardlen;
            ids[*num] = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
            if(!ids[*num]){
                HeapFree(GetProcessHeap(), 0, info);
                return E_OUTOFMEMORY;
            }
            memcpy(ids[*num], cardnameW, cardlen * sizeof(WCHAR));
            memcpy(ids[*num] + cardlen, dashW, lstrlenW(dashW) * sizeof(WCHAR));
            cardlen += lstrlenW(dashW);
            MultiByteToWideChar(CP_UNIXCP, 0, devname, -1, ids[*num] + cardlen,
                    len - cardlen);

            keys[*num] = HeapAlloc(GetProcessHeap(), 0, 32);
            if(!keys[*num]){
                HeapFree(GetProcessHeap(), 0, info);
                HeapFree(GetProcessHeap(), 0, ids[*num]);
                return E_OUTOFMEMORY;
            }
            memcpy(keys[*num], devnode, sizeof(devnode));
        }

        ++(*num);
    }

    HeapFree(GetProcessHeap(), 0, info);

    if(err != 0)
        WARN("Got a failure during device enumeration on card %d: %d (%s)\n",
                card, err, snd_strerror(err));

    return S_OK;
}

static HRESULT alsa_enum_devices(EDataFlow flow, WCHAR **ids, char **keys,
        UINT *num)
{
    snd_pcm_stream_t stream = (flow == eRender ? SND_PCM_STREAM_PLAYBACK :
        SND_PCM_STREAM_CAPTURE);
    int err, card;

    card = -1;
    *num = 0;

    if(alsa_try_open(defname, stream)){
        if(ids && keys){
            *ids = HeapAlloc(GetProcessHeap(), 0, sizeof(defaultW));
            memcpy(*ids, defaultW, sizeof(defaultW));
            *keys = HeapAlloc(GetProcessHeap(), 0, sizeof(defname));
            memcpy(*keys, defname, sizeof(defname));
        }
        ++*num;
    }

    for(err = snd_card_next(&card); card != -1 && err >= 0;
            err = snd_card_next(&card)){
        char cardpath[64];
        const char *cardname;
        WCHAR *cardnameW;
        snd_ctl_t *ctl;
        DWORD len;

        sprintf(cardpath, "hw:%u", card);

        if((err = snd_ctl_open(&ctl, cardpath, 0)) < 0){
            WARN("Unable to open ctl for ALSA device %s: %d (%s)\n", cardpath,
                    err, snd_strerror(err));
            continue;
        }

        if((err = snd_card_get_name(card, (char **)&cardname)) < 0){
            WARN("Unable to get card name for ALSA device %s: %d (%s)\n",
                    cardpath, err, snd_strerror(err));
            /* FIXME: Should be localized */
            cardname = "Unknown soundcard";
        }

        len = MultiByteToWideChar(CP_UNIXCP, 0, cardname, -1, NULL, 0);
        cardnameW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        if(!cardnameW){
            snd_ctl_close(ctl);
            return E_OUTOFMEMORY;
        }
        MultiByteToWideChar(CP_UNIXCP, 0, cardname, -1, cardnameW, len);

        alsa_get_card_devices(stream, ids, keys, num, ctl, card, cardnameW);

        HeapFree(GetProcessHeap(), 0, cardnameW);

        snd_ctl_close(ctl);
    }

    if(err != 0)
        WARN("Got a failure during card enumeration: %d (%s)\n",
                err, snd_strerror(err));

    return S_OK;
}

HRESULT WINAPI AUDDRV_GetEndpointIDs(EDataFlow flow, WCHAR ***ids, char ***keys,
        UINT *num, UINT *def_index)
{
    HRESULT hr;

    TRACE("%d %p %p %p %p\n", flow, ids, keys, num, def_index);

    hr = alsa_enum_devices(flow, NULL, NULL, num);
    if(FAILED(hr))
        return hr;

    if(*num == 0)
    {
        *ids = NULL;
        *keys = NULL;
        return S_OK;
    }

    *ids = HeapAlloc(GetProcessHeap(), 0, *num * sizeof(WCHAR *));
    *keys = HeapAlloc(GetProcessHeap(), 0, *num * sizeof(char *));
    if(!*ids || !*keys){
        HeapFree(GetProcessHeap(), 0, *ids);
        HeapFree(GetProcessHeap(), 0, *keys);
        return E_OUTOFMEMORY;
    }

    *def_index = 0;

    hr = alsa_enum_devices(flow, *ids, *keys, num);
    if(FAILED(hr)){
        int i;
        for(i = 0; i < *num; ++i){
            HeapFree(GetProcessHeap(), 0, (*ids)[i]);
            HeapFree(GetProcessHeap(), 0, (*keys)[i]);
        }
        HeapFree(GetProcessHeap(), 0, *ids);
        HeapFree(GetProcessHeap(), 0, *keys);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

/* Using the pulse PCM device from alsa-plugins 1.0.24 triggers a bug
 * which causes audio to cease playing after a few minutes of playback.
 * Setting handle_underrun=1 on pulse-backed ALSA devices seems to work
 * around this issue. */
static snd_config_t *make_handle_underrun_config(const char *name)
{
    snd_config_t *lconf, *dev_node, *hu_node, *type_node;
    char dev_node_name[64];
    const char *type_str;
    int err;

    snd_config_update();

    if((err = snd_config_copy(&lconf, snd_config)) < 0){
        WARN("snd_config_copy failed: %d (%s)\n", err, snd_strerror(err));
        return NULL;
    }

    sprintf(dev_node_name, "pcm.%s", name);
    err = snd_config_search(lconf, dev_node_name, &dev_node);
    if(err == -ENOENT){
        snd_config_delete(lconf);
        return NULL;
    }
    if(err < 0){
        snd_config_delete(lconf);
        WARN("snd_config_search failed: %d (%s)\n", err, snd_strerror(err));
        return NULL;
    }

    /* ALSA is extremely fragile. If it runs into a config setting it doesn't
     * recognize, it tends to fail or assert. So we only want to inject
     * handle_underrun=1 on devices that we know will recognize it. */
    err = snd_config_search(dev_node, "type", &type_node);
    if(err == -ENOENT){
        snd_config_delete(lconf);
        return NULL;
    }
    if(err < 0){
        snd_config_delete(lconf);
        WARN("snd_config_search failed: %d (%s)\n", err, snd_strerror(err));
        return NULL;
    }

    if((err = snd_config_get_string(type_node, &type_str)) < 0){
        snd_config_delete(lconf);
        return NULL;
    }

    if(strcmp(type_str, "pulse") != 0){
        snd_config_delete(lconf);
        return NULL;
    }

    err = snd_config_search(dev_node, "handle_underrun", &hu_node);
    if(err >= 0){
        /* user already has an explicit handle_underrun setting, so don't
         * use a local config */
        snd_config_delete(lconf);
        return NULL;
    }
    if(err != -ENOENT){
        snd_config_delete(lconf);
        WARN("snd_config_search failed: %d (%s)\n", err, snd_strerror(err));
        return NULL;
    }

    if((err = snd_config_imake_integer(&hu_node, "handle_underrun", 1)) < 0){
        snd_config_delete(lconf);
        WARN("snd_config_imake_integer failed: %d (%s)\n", err,
                snd_strerror(err));
        return NULL;
    }

    if((err = snd_config_add(dev_node, hu_node)) < 0){
        snd_config_delete(lconf);
        WARN("snd_config_add failed: %d (%s)\n", err, snd_strerror(err));
        return NULL;
    }

    return lconf;
}

HRESULT WINAPI AUDDRV_GetAudioEndpoint(const char *key, IMMDevice *dev,
        EDataFlow dataflow, IAudioClient **out)
{
    ACImpl *This;
    int err;
    snd_pcm_stream_t stream;
    snd_config_t *lconf;
    static int handle_underrun = 1;

    TRACE("\"%s\" %p %d %p\n", key, dev, dataflow, out);

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ACImpl));
    if(!This)
        return E_OUTOFMEMORY;

    This->IAudioClient_iface.lpVtbl = &AudioClient_Vtbl;
    This->IAudioRenderClient_iface.lpVtbl = &AudioRenderClient_Vtbl;
    This->IAudioCaptureClient_iface.lpVtbl = &AudioCaptureClient_Vtbl;
    This->IAudioClock_iface.lpVtbl = &AudioClock_Vtbl;
    This->IAudioClock2_iface.lpVtbl = &AudioClock2_Vtbl;
    This->IAudioStreamVolume_iface.lpVtbl = &AudioStreamVolume_Vtbl;

    if(dataflow == eRender)
        stream = SND_PCM_STREAM_PLAYBACK;
    else if(dataflow == eCapture)
        stream = SND_PCM_STREAM_CAPTURE;
    else{
        HeapFree(GetProcessHeap(), 0, This);
        return E_UNEXPECTED;
    }

    This->dataflow = dataflow;
    if(handle_underrun && ((lconf = make_handle_underrun_config(key)))){
        err = snd_pcm_open_lconf(&This->pcm_handle, key, stream, SND_PCM_NONBLOCK, lconf);
        TRACE("Opening PCM device \"%s\" with handle_underrun: %d\n", key, err);
        snd_config_delete(lconf);
        /* Pulse <= 2010 returns EINVAL, it does not know handle_underrun. */
        if(err == -EINVAL){
            ERR_(winediag)("PulseAudio \"%s\" %d without handle_underrun. Audio may hang."
                           " Please upgrade to alsa_plugins >= 1.0.24\n", key, err);
            handle_underrun = 0;
        }
    }else
        err = -EINVAL;
    if(err == -EINVAL){
        err = snd_pcm_open(&This->pcm_handle, key, stream, SND_PCM_NONBLOCK);
    }
    if(err < 0){
        HeapFree(GetProcessHeap(), 0, This);
        WARN("Unable to open PCM \"%s\": %d (%s)\n", key, err, snd_strerror(err));
        return E_FAIL;
    }

    This->hw_params = HeapAlloc(GetProcessHeap(), 0,
            snd_pcm_hw_params_sizeof());
    if(!This->hw_params){
        snd_pcm_close(This->pcm_handle);
        HeapFree(GetProcessHeap(), 0, This);
        return E_OUTOFMEMORY;
    }

    InitializeCriticalSection(&This->lock);
    This->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": ACImpl.lock");

    This->parent = dev;
    IMMDevice_AddRef(This->parent);

    *out = &This->IAudioClient_iface;
    IAudioClient_AddRef(&This->IAudioClient_iface);

    return S_OK;
}

static HRESULT WINAPI AudioClient_QueryInterface(IAudioClient *iface,
        REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioClient))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }
    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClient_AddRef(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioClient_Release(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if(!ref){
        IAudioClient_Stop(iface);
        IMMDevice_Release(This->parent);
        This->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->lock);
        snd_pcm_drop(This->pcm_handle);
        snd_pcm_close(This->pcm_handle);
        if(This->initted){
            EnterCriticalSection(&g_sessions_lock);
            list_remove(&This->entry);
            LeaveCriticalSection(&g_sessions_lock);
        }
        HeapFree(GetProcessHeap(), 0, This->vols);
        HeapFree(GetProcessHeap(), 0, This->local_buffer);
        HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
        HeapFree(GetProcessHeap(), 0, This->hw_params);
        CoTaskMemFree(This->fmt);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static void dump_fmt(const WAVEFORMATEX *fmt)
{
    TRACE("wFormatTag: 0x%x (", fmt->wFormatTag);
    switch(fmt->wFormatTag){
    case WAVE_FORMAT_PCM:
        TRACE("WAVE_FORMAT_PCM");
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        TRACE("WAVE_FORMAT_IEEE_FLOAT");
        break;
    case WAVE_FORMAT_EXTENSIBLE:
        TRACE("WAVE_FORMAT_EXTENSIBLE");
        break;
    default:
        TRACE("Unknown");
        break;
    }
    TRACE(")\n");

    TRACE("nChannels: %u\n", fmt->nChannels);
    TRACE("nSamplesPerSec: %u\n", fmt->nSamplesPerSec);
    TRACE("nAvgBytesPerSec: %u\n", fmt->nAvgBytesPerSec);
    TRACE("nBlockAlign: %u\n", fmt->nBlockAlign);
    TRACE("wBitsPerSample: %u\n", fmt->wBitsPerSample);
    TRACE("cbSize: %u\n", fmt->cbSize);

    if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
        WAVEFORMATEXTENSIBLE *fmtex = (void*)fmt;
        TRACE("dwChannelMask: %08x\n", fmtex->dwChannelMask);
        TRACE("Samples: %04x\n", fmtex->Samples.wReserved);
        TRACE("SubFormat: %s\n", wine_dbgstr_guid(&fmtex->SubFormat));
    }
}

static WAVEFORMATEX *clone_format(const WAVEFORMATEX *fmt)
{
    WAVEFORMATEX *ret;
    size_t size;

    if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        size = sizeof(WAVEFORMATEXTENSIBLE);
    else
        size = sizeof(WAVEFORMATEX);

    ret = CoTaskMemAlloc(size);
    if(!ret)
        return NULL;

    memcpy(ret, fmt, size);

    ret->cbSize = size - sizeof(WAVEFORMATEX);

    return ret;
}

static snd_pcm_format_t alsa_format(const WAVEFORMATEX *fmt)
{
    snd_pcm_format_t format = SND_PCM_FORMAT_UNKNOWN;
    const WAVEFORMATEXTENSIBLE *fmtex = (const WAVEFORMATEXTENSIBLE *)fmt;

    if(fmt->wFormatTag == WAVE_FORMAT_PCM ||
      (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
       IsEqualGUID(&fmtex->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))){
        if(fmt->wBitsPerSample == 8)
            format = SND_PCM_FORMAT_U8;
        else if(fmt->wBitsPerSample == 16)
            format = SND_PCM_FORMAT_S16_LE;
        else if(fmt->wBitsPerSample == 24)
            format = SND_PCM_FORMAT_S24_3LE;
        else if(fmt->wBitsPerSample == 32)
            format = SND_PCM_FORMAT_S32_LE;
        else
            WARN("Unsupported bit depth: %u\n", fmt->wBitsPerSample);
        if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
           fmt->wBitsPerSample != fmtex->Samples.wValidBitsPerSample){
            if(fmtex->Samples.wValidBitsPerSample == 20 && fmt->wBitsPerSample == 24)
                format = SND_PCM_FORMAT_S20_3LE;
            else{
                WARN("Unsupported ValidBits: %u\n", fmtex->Samples.wValidBitsPerSample);
                format = SND_PCM_FORMAT_UNKNOWN;
            }
        }
    }else if(fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
             IsEqualGUID(&fmtex->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))){
        if(fmt->wBitsPerSample == 32)
            format = SND_PCM_FORMAT_FLOAT_LE;
        else if(fmt->wBitsPerSample == 64)
            format = SND_PCM_FORMAT_FLOAT64_LE;
        else
            WARN("Unsupported float size: %u\n", fmt->wBitsPerSample);
    }else
        WARN("Unknown wave format: %04x\n", fmt->wFormatTag);
    return format;
}

static void session_init_vols(AudioSession *session, UINT channels)
{
    if(session->channel_count < channels){
        UINT i;

        if(session->channel_vols)
            session->channel_vols = HeapReAlloc(GetProcessHeap(), 0,
                    session->channel_vols, sizeof(float) * channels);
        else
            session->channel_vols = HeapAlloc(GetProcessHeap(), 0,
                    sizeof(float) * channels);
        if(!session->channel_vols)
            return;

        for(i = session->channel_count; i < channels; ++i)
            session->channel_vols[i] = 1.f;

        session->channel_count = channels;
    }
}

static AudioSession *create_session(const GUID *guid, IMMDevice *device,
        UINT num_channels)
{
    AudioSession *ret;

    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AudioSession));
    if(!ret)
        return NULL;

    memcpy(&ret->guid, guid, sizeof(GUID));

    ret->device = device;

    list_init(&ret->clients);

    list_add_head(&g_sessions, &ret->entry);

    InitializeCriticalSection(&ret->lock);
    ret->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": AudioSession.lock");

    session_init_vols(ret, num_channels);

    ret->master_vol = 1.f;

    return ret;
}

/* if channels == 0, then this will return or create a session with
 * matching dataflow and GUID. otherwise, channels must also match */
static HRESULT get_audio_session(const GUID *sessionguid,
        IMMDevice *device, UINT channels, AudioSession **out)
{
    AudioSession *session;

    if(!sessionguid || IsEqualGUID(sessionguid, &GUID_NULL)){
        *out = create_session(&GUID_NULL, device, channels);
        if(!*out)
            return E_OUTOFMEMORY;

        return S_OK;
    }

    *out = NULL;
    LIST_FOR_EACH_ENTRY(session, &g_sessions, AudioSession, entry){
        if(session->device == device &&
                IsEqualGUID(sessionguid, &session->guid)){
            session_init_vols(session, channels);
            *out = session;
            break;
        }
    }

    if(!*out){
        *out = create_session(sessionguid, device, channels);
        if(!*out)
            return E_OUTOFMEMORY;
    }

    return S_OK;
}

static HRESULT WINAPI AudioClient_Initialize(IAudioClient *iface,
        AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
        REFERENCE_TIME period, const WAVEFORMATEX *fmt,
        const GUID *sessionguid)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    snd_pcm_sw_params_t *sw_params = NULL;
    snd_pcm_format_t format;
    unsigned int rate, alsa_period_us;
    int err, i;
    HRESULT hr = S_OK;

    TRACE("(%p)->(%x, %x, %s, %s, %p, %s)\n", This, mode, flags,
          wine_dbgstr_longlong(duration), wine_dbgstr_longlong(period), fmt, debugstr_guid(sessionguid));

    if(!fmt)
        return E_POINTER;

    if(mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return AUDCLNT_E_NOT_INITIALIZED;

    if(flags & ~(AUDCLNT_STREAMFLAGS_CROSSPROCESS |
                AUDCLNT_STREAMFLAGS_LOOPBACK |
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_NOPERSIST |
                AUDCLNT_STREAMFLAGS_RATEADJUST |
                AUDCLNT_SESSIONFLAGS_EXPIREWHENUNOWNED |
                AUDCLNT_SESSIONFLAGS_DISPLAY_HIDE |
                AUDCLNT_SESSIONFLAGS_DISPLAY_HIDEWHENEXPIRED)){
        TRACE("Unknown flags: %08x\n", flags);
        return E_INVALIDARG;
    }

    if(mode == AUDCLNT_SHAREMODE_SHARED){
        period = DefaultPeriod;
        if( duration < 3 * period)
            duration = 3 * period;
    }else{
        if(!period)
            period = DefaultPeriod; /* not minimum */
        if(period < MinimumPeriod || period > 5000000)
            return AUDCLNT_E_INVALID_DEVICE_PERIOD;
        if(duration > 20000000) /* the smaller the period, the lower this limit */
            return AUDCLNT_E_BUFFER_SIZE_ERROR;
        if(flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK){
            if(duration != period)
                return AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL;
            FIXME("EXCLUSIVE mode with EVENTCALLBACK\n");
            return AUDCLNT_E_DEVICE_IN_USE;
        }else{
            if( duration < 8 * period)
                duration = 8 * period; /* may grow above 2s */
        }
    }

    EnterCriticalSection(&This->lock);

    if(This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_ALREADY_INITIALIZED;
    }

    dump_fmt(fmt);

    if((err = snd_pcm_hw_params_any(This->pcm_handle, This->hw_params)) < 0){
        WARN("Unable to get hw_params: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_hw_params_set_access(This->pcm_handle, This->hw_params,
                SND_PCM_ACCESS_RW_INTERLEAVED)) < 0){
        WARN("Unable to set access: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    format = alsa_format(fmt);
    if (format == SND_PCM_FORMAT_UNKNOWN){
        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
        goto exit;
    }

    if((err = snd_pcm_hw_params_set_format(This->pcm_handle, This->hw_params,
                format)) < 0){
        WARN("Unable to set ALSA format to %u: %d (%s)\n", format, err,
                snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    This->alsa_format = format;

    rate = fmt->nSamplesPerSec;
    if((err = snd_pcm_hw_params_set_rate_near(This->pcm_handle, This->hw_params,
                &rate, NULL)) < 0){
        WARN("Unable to set rate to %u: %d (%s)\n", rate, err,
                snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_hw_params_set_channels(This->pcm_handle, This->hw_params,
                fmt->nChannels)) < 0){
        WARN("Unable to set channels to %u: %d (%s)\n", fmt->nChannels, err,
                snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    This->mmdev_period_rt = period;
    alsa_period_us = This->mmdev_period_rt / 10;
    if((err = snd_pcm_hw_params_set_period_time_near(This->pcm_handle,
                This->hw_params, &alsa_period_us, NULL)) < 0)
        WARN("Unable to set period time near %u: %d (%s)\n", alsa_period_us,
                err, snd_strerror(err));
    /* ALSA updates the output variable alsa_period_us */

    This->mmdev_period_frames = MulDiv(fmt->nSamplesPerSec,
            This->mmdev_period_rt, 10000000);

    This->bufsize_frames = MulDiv(duration, fmt->nSamplesPerSec, 10000000);

    /* Buffer 4 ALSA periods if large enough, else 4 mmdevapi periods */
    This->alsa_bufsize_frames = This->mmdev_period_frames * 4;
    if(err < 0 || alsa_period_us < period / 10)
        err = snd_pcm_hw_params_set_buffer_size_near(This->pcm_handle,
                This->hw_params, &This->alsa_bufsize_frames);
    else{
        unsigned int periods = 4;
        err = snd_pcm_hw_params_set_periods_near(This->pcm_handle, This->hw_params, &periods, NULL);
    }
    if(err < 0)
        WARN("Unable to set buffer size: %d (%s)\n", err, snd_strerror(err));

    if((err = snd_pcm_hw_params(This->pcm_handle, This->hw_params)) < 0){
        WARN("Unable to set hw params: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_hw_params_get_period_size(This->hw_params,
                    &This->alsa_period_frames, NULL)) < 0){
        WARN("Unable to get period size: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_hw_params_get_buffer_size(This->hw_params,
                    &This->alsa_bufsize_frames)) < 0){
        WARN("Unable to get buffer size: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    sw_params = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, snd_pcm_sw_params_sizeof());
    if(!sw_params){
        hr = E_OUTOFMEMORY;
        goto exit;
    }

    if((err = snd_pcm_sw_params_current(This->pcm_handle, sw_params)) < 0){
        WARN("Unable to get sw_params: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_sw_params_set_start_threshold(This->pcm_handle,
                    sw_params, 1)) < 0){
        WARN("Unable set start threshold to 0: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_sw_params_set_stop_threshold(This->pcm_handle,
                    sw_params, This->alsa_bufsize_frames)) < 0){
        WARN("Unable set stop threshold to %lu: %d (%s)\n",
                This->alsa_bufsize_frames, err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_sw_params(This->pcm_handle, sw_params)) < 0){
        WARN("Unable set sw params: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_prepare(This->pcm_handle)) < 0){
        WARN("Unable to prepare device: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    /* Check if the ALSA buffer is so small that it will run out before
     * the next MMDevAPI period tick occurs. Allow a little wiggle room
     * with 120% of the period time. */
    if(This->alsa_bufsize_frames < 1.2 * This->mmdev_period_frames)
        FIXME("ALSA buffer time is too small. Expect underruns. (%lu < %u * 1.2)\n",
                This->alsa_bufsize_frames, This->mmdev_period_frames);

    This->local_buffer = HeapAlloc(GetProcessHeap(), 0,
            This->bufsize_frames * fmt->nBlockAlign);
    if(!This->local_buffer){
        hr = E_OUTOFMEMORY;
        goto exit;
    }
    if (fmt->wBitsPerSample == 8)
        memset(This->local_buffer, 128, This->bufsize_frames * fmt->nBlockAlign);
    else
        memset(This->local_buffer, 0, This->bufsize_frames * fmt->nBlockAlign);

    This->fmt = clone_format(fmt);
    if(!This->fmt){
        hr = E_OUTOFMEMORY;
        goto exit;
    }

    This->vols = HeapAlloc(GetProcessHeap(), 0, fmt->nChannels * sizeof(float));
    if(!This->vols){
        hr = E_OUTOFMEMORY;
        goto exit;
    }

    for(i = 0; i < fmt->nChannels; ++i)
        This->vols[i] = 1.f;

    This->share = mode;
    This->flags = flags;

    EnterCriticalSection(&g_sessions_lock);

    hr = get_audio_session(sessionguid, This->parent, fmt->nChannels,
            &This->session);
    if(FAILED(hr)){
        LeaveCriticalSection(&g_sessions_lock);
        goto exit;
    }

    list_add_tail(&This->session->clients, &This->entry);

    LeaveCriticalSection(&g_sessions_lock);

    This->initted = TRUE;

    TRACE("ALSA period: %lu frames\n", This->alsa_period_frames);
    TRACE("ALSA buffer: %lu frames\n", This->alsa_bufsize_frames);
    TRACE("MMDevice period: %u frames\n", This->mmdev_period_frames);
    TRACE("MMDevice buffer: %u frames\n", This->bufsize_frames);

exit:
    HeapFree(GetProcessHeap(), 0, sw_params);
    if(FAILED(hr)){
        HeapFree(GetProcessHeap(), 0, This->local_buffer);
        This->local_buffer = NULL;
        CoTaskMemFree(This->fmt);
        This->fmt = NULL;
        HeapFree(GetProcessHeap(), 0, This->vols);
        This->vols = NULL;
    }

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT WINAPI AudioClient_GetBufferSize(IAudioClient *iface,
        UINT32 *out)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%p)\n", This, out);

    if(!out)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    *out = This->bufsize_frames;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetStreamLatency(IAudioClient *iface,
        REFERENCE_TIME *latency)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%p)\n", This, latency);

    if(!latency)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    LeaveCriticalSection(&This->lock);

    /* one mmdevapi period plus one period we hide in the ALSA buffer */
    *latency = MulDiv(This->alsa_period_frames, 10000000, This->fmt->nSamplesPerSec)
             + This->mmdev_period_rt;

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetCurrentPadding(IAudioClient *iface,
        UINT32 *out)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%p)\n", This, out);

    if(!out)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    *out = This->held_frames;

    /* call required to get accurate snd_pcm_state() */
    snd_pcm_avail_update(This->pcm_handle);
    TRACE("pad: %u, state: %u\n", *out, snd_pcm_state(This->pcm_handle));

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static DWORD get_channel_mask(unsigned int channels)
{
    switch(channels){
    case 0:
        return 0;
    case 1:
        return KSAUDIO_SPEAKER_MONO;
    case 2:
        return KSAUDIO_SPEAKER_STEREO;
    case 3:
        return KSAUDIO_SPEAKER_STEREO | SPEAKER_LOW_FREQUENCY;
    case 4:
        return KSAUDIO_SPEAKER_QUAD;    /* not _SURROUND */
    case 5:
        return KSAUDIO_SPEAKER_QUAD | SPEAKER_LOW_FREQUENCY;
    case 6:
        return KSAUDIO_SPEAKER_5POINT1; /* not 5POINT1_SURROUND */
    case 7:
        return KSAUDIO_SPEAKER_5POINT1 | SPEAKER_BACK_CENTER;
    case 8:
        return KSAUDIO_SPEAKER_7POINT1_SURROUND; /* Vista deprecates 7POINT1 */
    }
    FIXME("Unknown speaker configuration: %u\n", channels);
    return 0;
}

static HRESULT WINAPI AudioClient_IsFormatSupported(IAudioClient *iface,
        AUDCLNT_SHAREMODE mode, const WAVEFORMATEX *fmt,
        WAVEFORMATEX **out)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    snd_pcm_format_mask_t *formats = NULL;
    snd_pcm_format_t format;
    HRESULT hr = S_OK;
    WAVEFORMATEX *closest = NULL;
    const WAVEFORMATEXTENSIBLE *fmtex = (const WAVEFORMATEXTENSIBLE *)fmt;
    unsigned int max = 0, min = 0;
    int err;

    TRACE("(%p)->(%x, %p, %p)\n", This, mode, fmt, out);

    if(!fmt || (mode == AUDCLNT_SHAREMODE_SHARED && !out))
        return E_POINTER;

    if(mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return E_INVALIDARG;

    if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            fmt->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return E_INVALIDARG;

    dump_fmt(fmt);

    if(out){
        *out = NULL;
        if(mode != AUDCLNT_SHAREMODE_SHARED)
            out = NULL;
    }

    EnterCriticalSection(&This->lock);

    if((err = snd_pcm_hw_params_any(This->pcm_handle, This->hw_params)) < 0){
        hr = E_FAIL;
        goto exit;
    }

    formats = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            snd_pcm_format_mask_sizeof());
    if(!formats){
        hr = E_OUTOFMEMORY;
        goto exit;
    }

    snd_pcm_hw_params_get_format_mask(This->hw_params, formats);
    format = alsa_format(fmt);
    if (format == SND_PCM_FORMAT_UNKNOWN ||
        !snd_pcm_format_mask_test(formats, format)){
        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
        goto exit;
    }

    closest = clone_format(fmt);
    if(!closest){
        hr = E_OUTOFMEMORY;
        goto exit;
    }

    if((err = snd_pcm_hw_params_get_rate_min(This->hw_params, &min, NULL)) < 0){
        hr = E_FAIL;
        WARN("Unable to get min rate: %d (%s)\n", err, snd_strerror(err));
        goto exit;
    }

    if((err = snd_pcm_hw_params_get_rate_max(This->hw_params, &max, NULL)) < 0){
        hr = E_FAIL;
        WARN("Unable to get max rate: %d (%s)\n", err, snd_strerror(err));
        goto exit;
    }

    if(fmt->nSamplesPerSec < min || fmt->nSamplesPerSec > max){
        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
        goto exit;
    }

    if((err = snd_pcm_hw_params_get_channels_min(This->hw_params, &min)) < 0){
        hr = E_FAIL;
        WARN("Unable to get min channels: %d (%s)\n", err, snd_strerror(err));
        goto exit;
    }

    if((err = snd_pcm_hw_params_get_channels_max(This->hw_params, &max)) < 0){
        hr = E_FAIL;
        WARN("Unable to get max channels: %d (%s)\n", err, snd_strerror(err));
        goto exit;
    }
    if(max > 8)
        max = 2;
    if(fmt->nChannels > max){
        hr = S_FALSE;
        closest->nChannels = max;
    }else if(fmt->nChannels < min){
        hr = S_FALSE;
        closest->nChannels = min;
    }

    if(closest->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
        DWORD mask = get_channel_mask(closest->nChannels);

        ((WAVEFORMATEXTENSIBLE*)closest)->dwChannelMask = mask;

        if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                fmtex->dwChannelMask != 0 &&
                fmtex->dwChannelMask != mask)
            hr = S_FALSE;
    }

exit:
    LeaveCriticalSection(&This->lock);
    HeapFree(GetProcessHeap(), 0, formats);

    if(hr == S_FALSE && !out)
        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;

    if(hr == S_FALSE && out) {
        closest->nBlockAlign =
            closest->nChannels * closest->wBitsPerSample / 8;
        closest->nAvgBytesPerSec =
            closest->nBlockAlign * closest->nSamplesPerSec;
        *out = closest;
    } else
        CoTaskMemFree(closest);

    TRACE("returning: %08x\n", hr);
    return hr;
}

static HRESULT WINAPI AudioClient_GetMixFormat(IAudioClient *iface,
        WAVEFORMATEX **pwfx)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    WAVEFORMATEXTENSIBLE *fmt;
    snd_pcm_format_mask_t *formats;
    unsigned int max_rate, max_channels;
    int err;
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p)\n", This, pwfx);

    if(!pwfx)
        return E_POINTER;
    *pwfx = NULL;

    fmt = CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
    if(!fmt)
        return E_OUTOFMEMORY;

    formats = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, snd_pcm_format_mask_sizeof());
    if(!formats){
        CoTaskMemFree(fmt);
        return E_OUTOFMEMORY;
    }

    EnterCriticalSection(&This->lock);

    if((err = snd_pcm_hw_params_any(This->pcm_handle, This->hw_params)) < 0){
        WARN("Unable to get hw_params: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    snd_pcm_hw_params_get_format_mask(This->hw_params, formats);

    fmt->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    if(snd_pcm_format_mask_test(formats, SND_PCM_FORMAT_FLOAT_LE)){
        fmt->Format.wBitsPerSample = 32;
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }else if(snd_pcm_format_mask_test(formats, SND_PCM_FORMAT_S16_LE)){
        fmt->Format.wBitsPerSample = 16;
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }else if(snd_pcm_format_mask_test(formats, SND_PCM_FORMAT_U8)){
        fmt->Format.wBitsPerSample = 8;
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }else if(snd_pcm_format_mask_test(formats, SND_PCM_FORMAT_S32_LE)){
        fmt->Format.wBitsPerSample = 32;
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }else if(snd_pcm_format_mask_test(formats, SND_PCM_FORMAT_S24_3LE)){
        fmt->Format.wBitsPerSample = 24;
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }else{
        ERR("Didn't recognize any available ALSA formats\n");
        hr = E_FAIL;
        goto exit;
    }

    if((err = snd_pcm_hw_params_get_channels_max(This->hw_params,
                    &max_channels)) < 0){
        WARN("Unable to get max channels: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if(max_channels > 2)
        fmt->Format.nChannels = 2;
    else
        fmt->Format.nChannels = max_channels;

    fmt->dwChannelMask = get_channel_mask(fmt->Format.nChannels);

    if((err = snd_pcm_hw_params_get_rate_max(This->hw_params, &max_rate,
                    NULL)) < 0){
        WARN("Unable to get max rate: %d (%s)\n", err, snd_strerror(err));
        hr = E_FAIL;
        goto exit;
    }

    if(max_rate >= 48000)
        fmt->Format.nSamplesPerSec = 48000;
    else if(max_rate >= 44100)
        fmt->Format.nSamplesPerSec = 44100;
    else if(max_rate >= 22050)
        fmt->Format.nSamplesPerSec = 22050;
    else if(max_rate >= 11025)
        fmt->Format.nSamplesPerSec = 11025;
    else if(max_rate >= 8000)
        fmt->Format.nSamplesPerSec = 8000;
    else{
        ERR("Unknown max rate: %u\n", max_rate);
        hr = E_FAIL;
        goto exit;
    }

    fmt->Format.nBlockAlign = (fmt->Format.wBitsPerSample *
            fmt->Format.nChannels) / 8;
    fmt->Format.nAvgBytesPerSec = fmt->Format.nSamplesPerSec *
        fmt->Format.nBlockAlign;

    fmt->Samples.wValidBitsPerSample = fmt->Format.wBitsPerSample;
    fmt->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

    dump_fmt((WAVEFORMATEX*)fmt);
    *pwfx = (WAVEFORMATEX*)fmt;

exit:
    LeaveCriticalSection(&This->lock);
    if(FAILED(hr))
        CoTaskMemFree(fmt);
    HeapFree(GetProcessHeap(), 0, formats);

    return hr;
}

static HRESULT WINAPI AudioClient_GetDevicePeriod(IAudioClient *iface,
        REFERENCE_TIME *defperiod, REFERENCE_TIME *minperiod)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%p, %p)\n", This, defperiod, minperiod);

    if(!defperiod && !minperiod)
        return E_POINTER;

    if(defperiod)
        *defperiod = DefaultPeriod;
    if(minperiod)
        *minperiod = MinimumPeriod;

    return S_OK;
}

static snd_pcm_sframes_t alsa_write_best_effort(snd_pcm_t *handle, BYTE *buf,
        snd_pcm_uframes_t frames, ACImpl *This)
{
    snd_pcm_sframes_t written;

    if(This->session->mute){
        int err;
        if((err = snd_pcm_format_set_silence(This->alsa_format, buf,
                        frames * This->fmt->nChannels)) < 0)
            WARN("Setting buffer to silence failed: %d (%s)\n", err,
                    snd_strerror(err));
    }

    written = snd_pcm_writei(handle, buf, frames);
    if(written < 0){
        int ret;

        if(written == -EAGAIN)
            /* buffer full */
            return 0;

        WARN("writei failed, recovering: %ld (%s)\n", written,
                snd_strerror(written));

        ret = snd_pcm_recover(handle, written, 0);
        if(ret < 0){
            WARN("Could not recover: %d (%s)\n", ret, snd_strerror(ret));
            return ret;
        }

        written = snd_pcm_writei(handle, buf, frames);
    }

    return written;
}

static void alsa_write_data(ACImpl *This)
{
    snd_pcm_sframes_t written;
    snd_pcm_uframes_t to_write, avail, write_limit, max_period, in_alsa;
    int err;
    BYTE *buf =
        This->local_buffer + (This->lcl_offs_frames * This->fmt->nBlockAlign);

    /* this call seems to be required to get an accurate snd_pcm_state() */
    avail = snd_pcm_avail_update(This->pcm_handle);

    if(snd_pcm_state(This->pcm_handle) == SND_PCM_STATE_XRUN ||
            avail > This->alsa_bufsize_frames){
        TRACE("XRun state, recovering\n");

        avail = This->alsa_bufsize_frames;

        if((err = snd_pcm_recover(This->pcm_handle, -EPIPE, 1)) < 0)
            WARN("snd_pcm_recover failed: %d (%s)\n", err, snd_strerror(err));

        if((err = snd_pcm_reset(This->pcm_handle)) < 0)
            WARN("snd_pcm_reset failed: %d (%s)\n", err, snd_strerror(err));

        if((err = snd_pcm_prepare(This->pcm_handle)) < 0)
            WARN("snd_pcm_prepare failed: %d (%s)\n", err, snd_strerror(err));
    }

    if(This->held_frames == 0)
        return;

    if(This->lcl_offs_frames + This->held_frames > This->bufsize_frames)
        to_write = This->bufsize_frames - This->lcl_offs_frames;
    else
        to_write = This->held_frames;

    max_period = max(This->mmdev_period_frames, This->alsa_period_frames);

    /* try to keep 3 ALSA periods or 3 MMDevAPI periods in the ALSA buffer and
     * no more */
    write_limit = 0;
    in_alsa = This->alsa_bufsize_frames - avail;
    while(in_alsa + write_limit < max_period * 3)
        write_limit += max_period;
    if(write_limit == 0)
        return;

    to_write = min(to_write, write_limit);

    written = alsa_write_best_effort(This->pcm_handle, buf, to_write, This);
    if(written < 0){
        WARN("Couldn't write: %ld (%s)\n", written, snd_strerror(written));
        return;
    }

    This->lcl_offs_frames += written;
    This->lcl_offs_frames %= This->bufsize_frames;
    This->held_frames -= written;

    if(written < to_write){
        /* ALSA buffer probably full */
        return;
    }

    if(This->held_frames && (written < write_limit)){
        /* wrapped and have some data back at the start to write */
        written = alsa_write_best_effort(This->pcm_handle, This->local_buffer,
                min(This->held_frames, write_limit - written), This);
        if(written < 0){
            WARN("Couldn't write: %ld (%s)\n", written, snd_strerror(written));
            return;
        }

        This->lcl_offs_frames += written;
        This->lcl_offs_frames %= This->bufsize_frames;
        This->held_frames -= written;
    }
}

static void alsa_read_data(ACImpl *This)
{
    snd_pcm_sframes_t pos, readable, nread;

    pos = (This->held_frames + This->lcl_offs_frames) % This->bufsize_frames;
    readable = This->bufsize_frames - pos;

    nread = snd_pcm_readi(This->pcm_handle,
            This->local_buffer + pos * This->fmt->nBlockAlign, readable);
    if(nread < 0){
        int ret;

        WARN("read failed, recovering: %ld (%s)\n", nread, snd_strerror(nread));

        ret = snd_pcm_recover(This->pcm_handle, nread, 0);
        if(ret < 0){
            WARN("Recover failed: %d (%s)\n", ret, snd_strerror(ret));
            return;
        }

        nread = snd_pcm_readi(This->pcm_handle,
                This->local_buffer + pos * This->fmt->nBlockAlign, readable);
        if(nread < 0){
            WARN("read failed: %ld (%s)\n", nread, snd_strerror(nread));
            return;
        }
    }

    if(This->session->mute){
        int err;
        if((err = snd_pcm_format_set_silence(This->alsa_format,
                        This->local_buffer + pos * This->fmt->nBlockAlign,
                        nread)) < 0)
            WARN("Setting buffer to silence failed: %d (%s)\n", err,
                    snd_strerror(err));
    }

    This->held_frames += nread;

    if(This->held_frames > This->bufsize_frames){
        WARN("Overflow of unread data\n");
        This->lcl_offs_frames += This->held_frames;
        This->lcl_offs_frames %= This->bufsize_frames;
        This->held_frames = This->bufsize_frames;
    }
}

static void CALLBACK alsa_push_buffer_data(void *user, BOOLEAN timer)
{
    ACImpl *This = user;

    EnterCriticalSection(&This->lock);

    if(This->started){
        if(This->dataflow == eRender)
            alsa_write_data(This);
        else if(This->dataflow == eCapture)
            alsa_read_data(This);

        if(This->event)
            SetEvent(This->event);
    }

    LeaveCriticalSection(&This->lock);
}

static HRESULT WINAPI AudioClient_Start(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)\n", This);

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if((This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) && !This->event){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_EVENTHANDLE_NOT_SET;
    }

    if(This->started){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    if(This->dataflow == eCapture){
        /* dump any data that might be leftover in the ALSA capture buffer */
        snd_pcm_readi(This->pcm_handle, This->local_buffer,
                This->bufsize_frames);
    }

    if(!CreateTimerQueueTimer(&This->timer, g_timer_q, alsa_push_buffer_data,
            This, 0, This->mmdev_period_rt / 10000, WT_EXECUTEINTIMERTHREAD)){
        LeaveCriticalSection(&This->lock);
        WARN("Unable to create timer: %u\n", GetLastError());
        return E_FAIL;
    }

    This->started = TRUE;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_Stop(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HANDLE event;
    BOOL wait;

    TRACE("(%p)\n", This);

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if(!This->started){
        LeaveCriticalSection(&This->lock);
        return S_FALSE;
    }

    /* Stop without losing written frames or position.
     * snd_pcm_pause would be appropriate but is unsupported by dmix.
     * snd_pcm_drain yields EAGAIN in NONBLOCK mode, except with Pulse. */

    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    wait = !DeleteTimerQueueTimer(g_timer_q, This->timer, event);
    if(wait)
        WARN("DeleteTimerQueueTimer error %u\n", GetLastError());
    wait = wait && GetLastError() == ERROR_IO_PENDING;

    This->started = FALSE;

    LeaveCriticalSection(&This->lock);

    if(event && wait)
        WaitForSingleObject(event, INFINITE);
    CloseHandle(event);

    return S_OK;
}

static HRESULT WINAPI AudioClient_Reset(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)\n", This);

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if(This->started){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    if(This->buf_state != NOT_LOCKED || This->getbuf_last){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_BUFFER_OPERATION_PENDING;
    }

    if(snd_pcm_drop(This->pcm_handle) < 0)
        WARN("snd_pcm_drop failed\n");

    if(snd_pcm_reset(This->pcm_handle) < 0)
        WARN("snd_pcm_reset failed\n");

    if(snd_pcm_prepare(This->pcm_handle) < 0)
        WARN("snd_pcm_prepare failed\n");

    if(This->dataflow == eRender){
        This->written_frames = 0;
        This->last_pos_frames = 0;
    }else{
        This->written_frames += This->held_frames;
    }
    This->held_frames = 0;
    This->lcl_offs_frames = 0;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_SetEventHandle(IAudioClient *iface,
        HANDLE event)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%p)\n", This, event);

    if(!event)
        return E_INVALIDARG;

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if(!(This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK)){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED;
    }

    This->event = event;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetService(IAudioClient *iface, REFIID riid,
        void **ppv)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    EnterCriticalSection(&This->lock);

    if(!This->initted){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if(IsEqualIID(riid, &IID_IAudioRenderClient)){
        if(This->dataflow != eRender){
            LeaveCriticalSection(&This->lock);
            return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
        }
        IAudioRenderClient_AddRef(&This->IAudioRenderClient_iface);
        *ppv = &This->IAudioRenderClient_iface;
    }else if(IsEqualIID(riid, &IID_IAudioCaptureClient)){
        if(This->dataflow != eCapture){
            LeaveCriticalSection(&This->lock);
            return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
        }
        IAudioCaptureClient_AddRef(&This->IAudioCaptureClient_iface);
        *ppv = &This->IAudioCaptureClient_iface;
    }else if(IsEqualIID(riid, &IID_IAudioClock)){
        IAudioClock_AddRef(&This->IAudioClock_iface);
        *ppv = &This->IAudioClock_iface;
    }else if(IsEqualIID(riid, &IID_IAudioStreamVolume)){
        IAudioStreamVolume_AddRef(&This->IAudioStreamVolume_iface);
        *ppv = &This->IAudioStreamVolume_iface;
    }else if(IsEqualIID(riid, &IID_IAudioSessionControl)){
        if(!This->session_wrapper){
            This->session_wrapper = AudioSessionWrapper_Create(This);
            if(!This->session_wrapper){
                LeaveCriticalSection(&This->lock);
                return E_OUTOFMEMORY;
            }
        }else
            IAudioSessionControl2_AddRef(&This->session_wrapper->IAudioSessionControl2_iface);

        *ppv = &This->session_wrapper->IAudioSessionControl2_iface;
    }else if(IsEqualIID(riid, &IID_IChannelAudioVolume)){
        if(!This->session_wrapper){
            This->session_wrapper = AudioSessionWrapper_Create(This);
            if(!This->session_wrapper){
                LeaveCriticalSection(&This->lock);
                return E_OUTOFMEMORY;
            }
        }else
            IChannelAudioVolume_AddRef(&This->session_wrapper->IChannelAudioVolume_iface);

        *ppv = &This->session_wrapper->IChannelAudioVolume_iface;
    }else if(IsEqualIID(riid, &IID_ISimpleAudioVolume)){
        if(!This->session_wrapper){
            This->session_wrapper = AudioSessionWrapper_Create(This);
            if(!This->session_wrapper){
                LeaveCriticalSection(&This->lock);
                return E_OUTOFMEMORY;
            }
        }else
            ISimpleAudioVolume_AddRef(&This->session_wrapper->ISimpleAudioVolume_iface);

        *ppv = &This->session_wrapper->ISimpleAudioVolume_iface;
    }

    if(*ppv){
        LeaveCriticalSection(&This->lock);
        return S_OK;
    }

    LeaveCriticalSection(&This->lock);

    FIXME("stub %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static const IAudioClientVtbl AudioClient_Vtbl =
{
    AudioClient_QueryInterface,
    AudioClient_AddRef,
    AudioClient_Release,
    AudioClient_Initialize,
    AudioClient_GetBufferSize,
    AudioClient_GetStreamLatency,
    AudioClient_GetCurrentPadding,
    AudioClient_IsFormatSupported,
    AudioClient_GetMixFormat,
    AudioClient_GetDevicePeriod,
    AudioClient_Start,
    AudioClient_Stop,
    AudioClient_Reset,
    AudioClient_SetEventHandle,
    AudioClient_GetService
};

static HRESULT WINAPI AudioRenderClient_QueryInterface(
        IAudioRenderClient *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioRenderClient))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioRenderClient_AddRef(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioRenderClient_Release(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioRenderClient_GetBuffer(IAudioRenderClient *iface,
        UINT32 frames, BYTE **data)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    UINT32 write_pos;
    UINT32 pad;
    HRESULT hr;

    TRACE("(%p)->(%u, %p)\n", This, frames, data);

    if(!data)
        return E_POINTER;
    *data = NULL;

    EnterCriticalSection(&This->lock);

    if(This->getbuf_last){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_OUT_OF_ORDER;
    }

    if(!frames){
        LeaveCriticalSection(&This->lock);
        return S_OK;
    }

    hr = IAudioClient_GetCurrentPadding(&This->IAudioClient_iface, &pad);
    if(FAILED(hr)){
        LeaveCriticalSection(&This->lock);
        return hr;
    }

    if(pad + frames > This->bufsize_frames){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    }

    write_pos =
        (This->lcl_offs_frames + This->held_frames) % This->bufsize_frames;
    if(write_pos + frames > This->bufsize_frames){
        if(This->tmp_buffer_frames < frames){
            HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
            This->tmp_buffer = HeapAlloc(GetProcessHeap(), 0,
                    frames * This->fmt->nBlockAlign);
            if(!This->tmp_buffer){
                LeaveCriticalSection(&This->lock);
                return E_OUTOFMEMORY;
            }
            This->tmp_buffer_frames = frames;
        }
        *data = This->tmp_buffer;
        This->getbuf_last = -frames;
    }else{
        *data = This->local_buffer + write_pos * This->fmt->nBlockAlign;
        This->getbuf_last = frames;
    }

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static void alsa_wrap_buffer(ACImpl *This, BYTE *buffer, UINT32 written_frames)
{
    snd_pcm_uframes_t write_offs_frames =
        (This->lcl_offs_frames + This->held_frames) % This->bufsize_frames;
    UINT32 write_offs_bytes = write_offs_frames * This->fmt->nBlockAlign;
    snd_pcm_uframes_t chunk_frames = This->bufsize_frames - write_offs_frames;
    UINT32 chunk_bytes = chunk_frames * This->fmt->nBlockAlign;
    UINT32 written_bytes = written_frames * This->fmt->nBlockAlign;

    if(written_bytes <= chunk_bytes){
        memcpy(This->local_buffer + write_offs_bytes, buffer, written_bytes);
    }else{
        memcpy(This->local_buffer + write_offs_bytes, buffer, chunk_bytes);
        memcpy(This->local_buffer, buffer + chunk_bytes,
                written_bytes - chunk_bytes);
    }
}

static HRESULT WINAPI AudioRenderClient_ReleaseBuffer(
        IAudioRenderClient *iface, UINT32 written_frames, DWORD flags)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    BYTE *buffer;

    TRACE("(%p)->(%u, %x)\n", This, written_frames, flags);

    EnterCriticalSection(&This->lock);

    if(!written_frames){
        This->getbuf_last = 0;
        LeaveCriticalSection(&This->lock);
        return S_OK;
    }

    if(!This->getbuf_last){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_OUT_OF_ORDER;
    }

    if(written_frames > (This->getbuf_last >= 0 ? This->getbuf_last : -This->getbuf_last)){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_INVALID_SIZE;
    }

    if(This->getbuf_last >= 0)
        buffer = This->local_buffer + This->fmt->nBlockAlign *
          ((This->lcl_offs_frames + This->held_frames) % This->bufsize_frames);
    else
        buffer = This->tmp_buffer;

    if(flags & AUDCLNT_BUFFERFLAGS_SILENT){
        if(This->fmt->wBitsPerSample == 8)
            memset(buffer, 128, written_frames * This->fmt->nBlockAlign);
        else
            memset(buffer, 0, written_frames * This->fmt->nBlockAlign);
    }

    if(This->getbuf_last < 0)
        alsa_wrap_buffer(This, buffer, written_frames);

    This->held_frames += written_frames;
    This->written_frames += written_frames;
    This->getbuf_last = 0;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static const IAudioRenderClientVtbl AudioRenderClient_Vtbl = {
    AudioRenderClient_QueryInterface,
    AudioRenderClient_AddRef,
    AudioRenderClient_Release,
    AudioRenderClient_GetBuffer,
    AudioRenderClient_ReleaseBuffer
};

static HRESULT WINAPI AudioCaptureClient_QueryInterface(
        IAudioCaptureClient *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioCaptureClient))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioCaptureClient_AddRef(IAudioCaptureClient *iface)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioCaptureClient_Release(IAudioCaptureClient *iface)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioCaptureClient_GetBuffer(IAudioCaptureClient *iface,
        BYTE **data, UINT32 *frames, DWORD *flags, UINT64 *devpos,
        UINT64 *qpcpos)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p, %p, %p, %p, %p)\n", This, data, frames, flags,
            devpos, qpcpos);

    if(!data || !frames || !flags)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if(This->buf_state != NOT_LOCKED){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_OUT_OF_ORDER;
    }

    hr = IAudioCaptureClient_GetNextPacketSize(iface, frames);
    if(FAILED(hr)){
        LeaveCriticalSection(&This->lock);
        return hr;
    }

    *flags = 0;

    if(This->lcl_offs_frames + *frames > This->bufsize_frames){
        UINT32 chunk_bytes, offs_bytes, frames_bytes;
        if(This->tmp_buffer_frames < *frames){
            HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
            This->tmp_buffer = HeapAlloc(GetProcessHeap(), 0,
                    *frames * This->fmt->nBlockAlign);
            if(!This->tmp_buffer){
                LeaveCriticalSection(&This->lock);
                return E_OUTOFMEMORY;
            }
            This->tmp_buffer_frames = *frames;
        }

        *data = This->tmp_buffer;
        chunk_bytes = (This->bufsize_frames - This->lcl_offs_frames) *
            This->fmt->nBlockAlign;
        offs_bytes = This->lcl_offs_frames * This->fmt->nBlockAlign;
        frames_bytes = *frames * This->fmt->nBlockAlign;
        memcpy(This->tmp_buffer, This->local_buffer + offs_bytes, chunk_bytes);
        memcpy(This->tmp_buffer + chunk_bytes, This->local_buffer,
                frames_bytes - chunk_bytes);
    }else
        *data = This->local_buffer +
            This->lcl_offs_frames * This->fmt->nBlockAlign;

    This->buf_state = LOCKED_NORMAL;

    if(devpos || qpcpos)
        IAudioClock_GetPosition(&This->IAudioClock_iface, devpos, qpcpos);

    LeaveCriticalSection(&This->lock);

    return *frames ? S_OK : AUDCLNT_S_BUFFER_EMPTY;
}

static HRESULT WINAPI AudioCaptureClient_ReleaseBuffer(
        IAudioCaptureClient *iface, UINT32 done)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);

    TRACE("(%p)->(%u)\n", This, done);

    EnterCriticalSection(&This->lock);

    if(This->buf_state == NOT_LOCKED){
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_OUT_OF_ORDER;
    }

    This->held_frames -= done;
    This->lcl_offs_frames += done;
    This->lcl_offs_frames %= This->bufsize_frames;

    This->buf_state = NOT_LOCKED;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioCaptureClient_GetNextPacketSize(
        IAudioCaptureClient *iface, UINT32 *frames)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);

    TRACE("(%p)->(%p)\n", This, frames);

    return AudioClient_GetCurrentPadding(&This->IAudioClient_iface, frames);
}

static const IAudioCaptureClientVtbl AudioCaptureClient_Vtbl =
{
    AudioCaptureClient_QueryInterface,
    AudioCaptureClient_AddRef,
    AudioCaptureClient_Release,
    AudioCaptureClient_GetBuffer,
    AudioCaptureClient_ReleaseBuffer,
    AudioCaptureClient_GetNextPacketSize
};

static HRESULT WINAPI AudioClock_QueryInterface(IAudioClock *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioClock))
        *ppv = iface;
    else if(IsEqualIID(riid, &IID_IAudioClock2))
        *ppv = &This->IAudioClock2_iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClock_AddRef(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioClock_Release(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioClock_GetFrequency(IAudioClock *iface, UINT64 *freq)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p)\n", This, freq);

    *freq = This->fmt->nSamplesPerSec;

    return S_OK;
}

static HRESULT WINAPI AudioClock_GetPosition(IAudioClock *iface, UINT64 *pos,
        UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    UINT64 written_frames, position;
    UINT32 held_frames;
    int err;
    snd_pcm_state_t alsa_state;
    snd_pcm_uframes_t avail_frames;
    snd_pcm_sframes_t delay_frames;

    TRACE("(%p)->(%p, %p)\n", This, pos, qpctime);

    if(!pos)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    /* call required to get accurate snd_pcm_state() */
    avail_frames = snd_pcm_avail_update(This->pcm_handle);
    alsa_state = snd_pcm_state(This->pcm_handle);
    written_frames = This->written_frames;
    held_frames = This->held_frames;

    err = snd_pcm_delay(This->pcm_handle, &delay_frames);
    if(err < 0){
        /* old Pulse, shortly after start */
        WARN("snd_pcm_delay failed in state %u: %d (%s)\n", alsa_state, err, snd_strerror(err));
    }

    if(This->dataflow == eRender){
        position = written_frames - held_frames; /* maximum */
        if(!This->started || alsa_state > SND_PCM_STATE_RUNNING)
            ; /* mmdevapi stopped or ALSA underrun: pretend everything was played */
        else if(err<0 || delay_frames > position - This->last_pos_frames)
            /* Pulse bug: past underrun, despite recovery, avail_frames & delay
             * may be larger than alsa_bufsize_frames, as if cumulating frames. */
            /* Pulse bug: EIO(-5) shortly after starting: nothing played */
            position = This->last_pos_frames;
        else if(delay_frames > 0)
            position -= delay_frames;
    }else
        position = written_frames + held_frames;

    /* ensure monotic growth */
    This->last_pos_frames = position;

    LeaveCriticalSection(&This->lock);

    TRACE("frames written: %u, held: %u, avail: %ld, delay: %ld state %d, pos: %u\n",
          (UINT32)(written_frames%1000000000), held_frames,
          avail_frames, delay_frames, alsa_state, (UINT32)(position%1000000000));
    *pos = position;

    if(qpctime){
        LARGE_INTEGER stamp, freq;
        QueryPerformanceCounter(&stamp);
        QueryPerformanceFrequency(&freq);
        *qpctime = (stamp.QuadPart * (INT64)10000000) / freq.QuadPart;
    }

    return S_OK;
}

static HRESULT WINAPI AudioClock_GetCharacteristics(IAudioClock *iface,
        DWORD *chars)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p)\n", This, chars);

    if(!chars)
        return E_POINTER;

    *chars = AUDIOCLOCK_CHARACTERISTIC_FIXED_FREQ;

    return S_OK;
}

static const IAudioClockVtbl AudioClock_Vtbl =
{
    AudioClock_QueryInterface,
    AudioClock_AddRef,
    AudioClock_Release,
    AudioClock_GetFrequency,
    AudioClock_GetPosition,
    AudioClock_GetCharacteristics
};

static HRESULT WINAPI AudioClock2_QueryInterface(IAudioClock2 *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClock_QueryInterface(&This->IAudioClock_iface, riid, ppv);
}

static ULONG WINAPI AudioClock2_AddRef(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioClock2_Release(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioClock2_GetDevicePosition(IAudioClock2 *iface,
        UINT64 *pos, UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock2(iface);

    FIXME("(%p)->(%p, %p)\n", This, pos, qpctime);

    return E_NOTIMPL;
}

static const IAudioClock2Vtbl AudioClock2_Vtbl =
{
    AudioClock2_QueryInterface,
    AudioClock2_AddRef,
    AudioClock2_Release,
    AudioClock2_GetDevicePosition
};

static AudioSessionWrapper *AudioSessionWrapper_Create(ACImpl *client)
{
    AudioSessionWrapper *ret;

    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(AudioSessionWrapper));
    if(!ret)
        return NULL;

    ret->IAudioSessionControl2_iface.lpVtbl = &AudioSessionControl2_Vtbl;
    ret->ISimpleAudioVolume_iface.lpVtbl = &SimpleAudioVolume_Vtbl;
    ret->IChannelAudioVolume_iface.lpVtbl = &ChannelAudioVolume_Vtbl;

    ret->ref = 1;

    ret->client = client;
    if(client){
        ret->session = client->session;
        AudioClient_AddRef(&client->IAudioClient_iface);
    }

    return ret;
}

static HRESULT WINAPI AudioSessionControl_QueryInterface(
        IAudioSessionControl2 *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioSessionControl) ||
            IsEqualIID(riid, &IID_IAudioSessionControl2))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioSessionControl_AddRef(IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioSessionControl_Release(IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if(!ref){
        if(This->client){
            EnterCriticalSection(&This->client->lock);
            This->client->session_wrapper = NULL;
            LeaveCriticalSection(&This->client->lock);
            AudioClient_Release(&This->client->IAudioClient_iface);
        }
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static HRESULT WINAPI AudioSessionControl_GetState(IAudioSessionControl2 *iface,
        AudioSessionState *state)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ACImpl *client;

    TRACE("(%p)->(%p)\n", This, state);

    if(!state)
        return NULL_PTR_ERR;

    EnterCriticalSection(&g_sessions_lock);

    if(list_empty(&This->session->clients)){
        *state = AudioSessionStateExpired;
        LeaveCriticalSection(&g_sessions_lock);
        return S_OK;
    }

    LIST_FOR_EACH_ENTRY(client, &This->session->clients, ACImpl, entry){
        EnterCriticalSection(&client->lock);
        if(client->started){
            *state = AudioSessionStateActive;
            LeaveCriticalSection(&client->lock);
            LeaveCriticalSection(&g_sessions_lock);
            return S_OK;
        }
        LeaveCriticalSection(&client->lock);
    }

    LeaveCriticalSection(&g_sessions_lock);

    *state = AudioSessionStateInactive;

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetDisplayName(
        IAudioSessionControl2 *iface, WCHAR **name)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, name);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetDisplayName(
        IAudioSessionControl2 *iface, const WCHAR *name, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p, %s) - stub\n", This, name, debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetIconPath(
        IAudioSessionControl2 *iface, WCHAR **path)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, path);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetIconPath(
        IAudioSessionControl2 *iface, const WCHAR *path, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p, %s) - stub\n", This, path, debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetGroupingParam(
        IAudioSessionControl2 *iface, GUID *group)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, group);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetGroupingParam(
        IAudioSessionControl2 *iface, const GUID *group, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%s, %s) - stub\n", This, debugstr_guid(group),
            debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_RegisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_UnregisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetSessionIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetSessionInstanceIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetProcessId(
        IAudioSessionControl2 *iface, DWORD *pid)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%p)\n", This, pid);

    if(!pid)
        return E_POINTER;

    *pid = GetCurrentProcessId();

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_IsSystemSoundsSession(
        IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)\n", This);

    return S_FALSE;
}

static HRESULT WINAPI AudioSessionControl_SetDuckingPreference(
        IAudioSessionControl2 *iface, BOOL optout)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%d)\n", This, optout);

    return S_OK;
}

static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl =
{
    AudioSessionControl_QueryInterface,
    AudioSessionControl_AddRef,
    AudioSessionControl_Release,
    AudioSessionControl_GetState,
    AudioSessionControl_GetDisplayName,
    AudioSessionControl_SetDisplayName,
    AudioSessionControl_GetIconPath,
    AudioSessionControl_SetIconPath,
    AudioSessionControl_GetGroupingParam,
    AudioSessionControl_SetGroupingParam,
    AudioSessionControl_RegisterAudioSessionNotification,
    AudioSessionControl_UnregisterAudioSessionNotification,
    AudioSessionControl_GetSessionIdentifier,
    AudioSessionControl_GetSessionInstanceIdentifier,
    AudioSessionControl_GetProcessId,
    AudioSessionControl_IsSystemSoundsSession,
    AudioSessionControl_SetDuckingPreference
};

static HRESULT WINAPI SimpleAudioVolume_QueryInterface(
        ISimpleAudioVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_ISimpleAudioVolume))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI SimpleAudioVolume_AddRef(ISimpleAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    return AudioSessionControl_AddRef(&This->IAudioSessionControl2_iface);
}

static ULONG WINAPI SimpleAudioVolume_Release(ISimpleAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    return AudioSessionControl_Release(&This->IAudioSessionControl2_iface);
}

static HRESULT WINAPI SimpleAudioVolume_SetMasterVolume(
        ISimpleAudioVolume *iface, float level, const GUID *context)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%f, %s)\n", session, level, wine_dbgstr_guid(context));

    if(level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if(context)
        FIXME("Notifications not supported yet\n");

    TRACE("ALSA does not support volume control\n");

    EnterCriticalSection(&session->lock);

    session->master_vol = level;

    LeaveCriticalSection(&session->lock);

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_GetMasterVolume(
        ISimpleAudioVolume *iface, float *level)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, level);

    if(!level)
        return NULL_PTR_ERR;

    *level = session->master_vol;

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_SetMute(ISimpleAudioVolume *iface,
        BOOL mute, const GUID *context)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%u, %p)\n", session, mute, context);

    if(context)
        FIXME("Notifications not supported yet\n");

    session->mute = mute;

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_GetMute(ISimpleAudioVolume *iface,
        BOOL *mute)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, mute);

    if(!mute)
        return NULL_PTR_ERR;

    *mute = session->mute;

    return S_OK;
}

static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl  =
{
    SimpleAudioVolume_QueryInterface,
    SimpleAudioVolume_AddRef,
    SimpleAudioVolume_Release,
    SimpleAudioVolume_SetMasterVolume,
    SimpleAudioVolume_GetMasterVolume,
    SimpleAudioVolume_SetMute,
    SimpleAudioVolume_GetMute
};

static HRESULT WINAPI AudioStreamVolume_QueryInterface(
        IAudioStreamVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioStreamVolume))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioStreamVolume_AddRef(IAudioStreamVolume *iface)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioStreamVolume_Release(IAudioStreamVolume *iface)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioStreamVolume_GetChannelCount(
        IAudioStreamVolume *iface, UINT32 *out)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%p)\n", This, out);

    if(!out)
        return E_POINTER;

    *out = This->fmt->nChannels;

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_SetChannelVolume(
        IAudioStreamVolume *iface, UINT32 index, float level)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%d, %f)\n", This, index, level);

    if(level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if(index >= This->fmt->nChannels)
        return E_INVALIDARG;

    TRACE("ALSA does not support volume control\n");

    EnterCriticalSection(&This->lock);

    This->vols[index] = level;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_GetChannelVolume(
        IAudioStreamVolume *iface, UINT32 index, float *level)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%d, %p)\n", This, index, level);

    if(!level)
        return E_POINTER;

    if(index >= This->fmt->nChannels)
        return E_INVALIDARG;

    *level = This->vols[index];

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_SetAllVolumes(
        IAudioStreamVolume *iface, UINT32 count, const float *levels)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    int i;

    TRACE("(%p)->(%d, %p)\n", This, count, levels);

    if(!levels)
        return E_POINTER;

    if(count != This->fmt->nChannels)
        return E_INVALIDARG;

    TRACE("ALSA does not support volume control\n");

    EnterCriticalSection(&This->lock);

    for(i = 0; i < count; ++i)
        This->vols[i] = levels[i];

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_GetAllVolumes(
        IAudioStreamVolume *iface, UINT32 count, float *levels)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    int i;

    TRACE("(%p)->(%d, %p)\n", This, count, levels);

    if(!levels)
        return E_POINTER;

    if(count != This->fmt->nChannels)
        return E_INVALIDARG;

    EnterCriticalSection(&This->lock);

    for(i = 0; i < count; ++i)
        levels[i] = This->vols[i];

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static const IAudioStreamVolumeVtbl AudioStreamVolume_Vtbl =
{
    AudioStreamVolume_QueryInterface,
    AudioStreamVolume_AddRef,
    AudioStreamVolume_Release,
    AudioStreamVolume_GetChannelCount,
    AudioStreamVolume_SetChannelVolume,
    AudioStreamVolume_GetChannelVolume,
    AudioStreamVolume_SetAllVolumes,
    AudioStreamVolume_GetAllVolumes
};

static HRESULT WINAPI ChannelAudioVolume_QueryInterface(
        IChannelAudioVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IChannelAudioVolume))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI ChannelAudioVolume_AddRef(IChannelAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    return AudioSessionControl_AddRef(&This->IAudioSessionControl2_iface);
}

static ULONG WINAPI ChannelAudioVolume_Release(IChannelAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    return AudioSessionControl_Release(&This->IAudioSessionControl2_iface);
}

static HRESULT WINAPI ChannelAudioVolume_GetChannelCount(
        IChannelAudioVolume *iface, UINT32 *out)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, out);

    if(!out)
        return NULL_PTR_ERR;

    *out = session->channel_count;

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_SetChannelVolume(
        IChannelAudioVolume *iface, UINT32 index, float level,
        const GUID *context)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%d, %f, %s)\n", session, index, level,
            wine_dbgstr_guid(context));

    if(level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if(index >= session->channel_count)
        return E_INVALIDARG;

    if(context)
        FIXME("Notifications not supported yet\n");

    TRACE("ALSA does not support volume control\n");

    EnterCriticalSection(&session->lock);

    session->channel_vols[index] = level;

    LeaveCriticalSection(&session->lock);

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_GetChannelVolume(
        IChannelAudioVolume *iface, UINT32 index, float *level)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%d, %p)\n", session, index, level);

    if(!level)
        return NULL_PTR_ERR;

    if(index >= session->channel_count)
        return E_INVALIDARG;

    *level = session->channel_vols[index];

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_SetAllVolumes(
        IChannelAudioVolume *iface, UINT32 count, const float *levels,
        const GUID *context)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;
    int i;

    TRACE("(%p)->(%d, %p, %s)\n", session, count, levels,
            wine_dbgstr_guid(context));

    if(!levels)
        return NULL_PTR_ERR;

    if(count != session->channel_count)
        return E_INVALIDARG;

    if(context)
        FIXME("Notifications not supported yet\n");

    TRACE("ALSA does not support volume control\n");

    EnterCriticalSection(&session->lock);

    for(i = 0; i < count; ++i)
        session->channel_vols[i] = levels[i];

    LeaveCriticalSection(&session->lock);

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_GetAllVolumes(
        IChannelAudioVolume *iface, UINT32 count, float *levels)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;
    int i;

    TRACE("(%p)->(%d, %p)\n", session, count, levels);

    if(!levels)
        return NULL_PTR_ERR;

    if(count != session->channel_count)
        return E_INVALIDARG;

    for(i = 0; i < count; ++i)
        levels[i] = session->channel_vols[i];

    return S_OK;
}

static const IChannelAudioVolumeVtbl ChannelAudioVolume_Vtbl =
{
    ChannelAudioVolume_QueryInterface,
    ChannelAudioVolume_AddRef,
    ChannelAudioVolume_Release,
    ChannelAudioVolume_GetChannelCount,
    ChannelAudioVolume_SetChannelVolume,
    ChannelAudioVolume_GetChannelVolume,
    ChannelAudioVolume_SetAllVolumes,
    ChannelAudioVolume_GetAllVolumes
};

static HRESULT WINAPI AudioSessionManager_QueryInterface(IAudioSessionManager2 *iface,
        REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioSessionManager) ||
            IsEqualIID(riid, &IID_IAudioSessionManager2))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioSessionManager_AddRef(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioSessionManager_Release(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if(!ref)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static HRESULT WINAPI AudioSessionManager_GetAudioSessionControl(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        IAudioSessionControl **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if(FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if(!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = (IAudioSessionControl*)&wrapper->IAudioSessionControl2_iface;

    return S_OK;
}

static HRESULT WINAPI AudioSessionManager_GetSimpleAudioVolume(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        ISimpleAudioVolume **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if(FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if(!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = &wrapper->ISimpleAudioVolume_iface;

    return S_OK;
}

static HRESULT WINAPI AudioSessionManager_GetSessionEnumerator(
        IAudioSessionManager2 *iface, IAudioSessionEnumerator **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, out);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_RegisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_UnregisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_RegisterDuckNotification(
        IAudioSessionManager2 *iface, const WCHAR *session_id,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_UnregisterDuckNotification(
        IAudioSessionManager2 *iface,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static const IAudioSessionManager2Vtbl AudioSessionManager2_Vtbl =
{
    AudioSessionManager_QueryInterface,
    AudioSessionManager_AddRef,
    AudioSessionManager_Release,
    AudioSessionManager_GetAudioSessionControl,
    AudioSessionManager_GetSimpleAudioVolume,
    AudioSessionManager_GetSessionEnumerator,
    AudioSessionManager_RegisterSessionNotification,
    AudioSessionManager_UnregisterSessionNotification,
    AudioSessionManager_RegisterDuckNotification,
    AudioSessionManager_UnregisterDuckNotification
};

HRESULT WINAPI AUDDRV_GetAudioSessionManager(IMMDevice *device,
        IAudioSessionManager2 **out)
{
    SessionMgr *This;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SessionMgr));
    if(!This)
        return E_OUTOFMEMORY;

    This->IAudioSessionManager2_iface.lpVtbl = &AudioSessionManager2_Vtbl;
    This->device = device;
    This->ref = 1;

    *out = &This->IAudioSessionManager2_iface;

    return S_OK;
}
