/*
 * Copyright 2019 Nikolay Sivov for CodeWeavers
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

#define COBJMACROS

#include "mfidl.h"
#include "mferror.h"
#include "mf_private.h"

#include "wine/debug.h"
#include "wine/heap.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

struct audio_renderer
{
    IMFMediaSink IMFMediaSink_iface;
    IMFMediaSinkPreroll IMFMediaSinkPreroll_iface;
    LONG refcount;
    BOOL is_shut_down;
    CRITICAL_SECTION cs;
};

static struct audio_renderer *impl_from_IMFMediaSink(IMFMediaSink *iface)
{
    return CONTAINING_RECORD(iface, struct audio_renderer, IMFMediaSink_iface);
}

static struct audio_renderer *impl_from_IMFMediaSinkPreroll(IMFMediaSinkPreroll *iface)
{
    return CONTAINING_RECORD(iface, struct audio_renderer, IMFMediaSinkPreroll_iface);
}

static HRESULT WINAPI audio_renderer_sink_QueryInterface(IMFMediaSink *iface, REFIID riid, void **obj)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSink(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IMFMediaSink) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
    }
    else if (IsEqualIID(riid, &IID_IMFMediaSinkPreroll))
    {
        *obj = &renderer->IMFMediaSinkPreroll_iface;
    }
    else
    {
        WARN("Unsupported %s.\n", debugstr_guid(riid));
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*obj);

    return S_OK;
}

static ULONG WINAPI audio_renderer_sink_AddRef(IMFMediaSink *iface)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSink(iface);
    ULONG refcount = InterlockedIncrement(&renderer->refcount);
    TRACE("%p, refcount %u.\n", iface, refcount);
    return refcount;
}

static ULONG WINAPI audio_renderer_sink_Release(IMFMediaSink *iface)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSink(iface);
    ULONG refcount = InterlockedDecrement(&renderer->refcount);

    TRACE("%p, refcount %u.\n", iface, refcount);

    if (!refcount)
    {
        DeleteCriticalSection(&renderer->cs);
        heap_free(renderer);
    }

    return refcount;
}

static HRESULT WINAPI audio_renderer_sink_GetCharacteristics(IMFMediaSink *iface, DWORD *flags)
{
    FIXME("%p, %p.\n", iface, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI audio_renderer_sink_AddStreamSink(IMFMediaSink *iface, DWORD stream_sink_id,
    IMFMediaType *media_type, IMFStreamSink **stream_sink)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSink(iface);

    TRACE("%p, %#x, %p, %p.\n", iface, stream_sink_id, media_type, stream_sink);

    return renderer->is_shut_down ? MF_E_SHUTDOWN : MF_E_STREAMSINKS_FIXED;
}

static HRESULT WINAPI audio_renderer_sink_RemoveStreamSink(IMFMediaSink *iface, DWORD stream_sink_id)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSink(iface);

    TRACE("%p, %#x.\n", iface, stream_sink_id);

    return renderer->is_shut_down ? MF_E_SHUTDOWN : MF_E_STREAMSINKS_FIXED;
}

static HRESULT WINAPI audio_renderer_sink_GetStreamSinkCount(IMFMediaSink *iface, DWORD *count)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSink(iface);

    TRACE("%p, %p.\n", iface, count);

    if (!count)
        return E_POINTER;

    if (renderer->is_shut_down)
        return MF_E_SHUTDOWN;

    *count = 1;

    return S_OK;
}

static HRESULT WINAPI audio_renderer_sink_GetStreamSinkByIndex(IMFMediaSink *iface, DWORD index,
        IMFStreamSink **stream)
{
    FIXME("%p, %u, %p.\n", iface, index, stream);

    return E_NOTIMPL;
}

static HRESULT WINAPI audio_renderer_sink_GetStreamSinkById(IMFMediaSink *iface, DWORD stream_sink_id,
        IMFStreamSink **stream)
{
    FIXME("%p, %#x, %p.\n", iface, stream_sink_id, stream);

    return E_NOTIMPL;
}

static HRESULT WINAPI audio_renderer_sink_SetPresentationClock(IMFMediaSink *iface, IMFPresentationClock *clock)
{
    FIXME("%p, %p.\n", iface, clock);

    return E_NOTIMPL;
}

static HRESULT WINAPI audio_renderer_sink_GetPresentationClock(IMFMediaSink *iface, IMFPresentationClock **clock)
{
    FIXME("%p, %p.\n", iface, clock);

    return E_NOTIMPL;
}

static HRESULT WINAPI audio_renderer_sink_Shutdown(IMFMediaSink *iface)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSink(iface);

    TRACE("%p.\n", iface);

    if (renderer->is_shut_down)
        return MF_E_SHUTDOWN;

    EnterCriticalSection(&renderer->cs);
    renderer->is_shut_down = TRUE;
    LeaveCriticalSection(&renderer->cs);

    return S_OK;
}

static const IMFMediaSinkVtbl audio_renderer_sink_vtbl =
{
    audio_renderer_sink_QueryInterface,
    audio_renderer_sink_AddRef,
    audio_renderer_sink_Release,
    audio_renderer_sink_GetCharacteristics,
    audio_renderer_sink_AddStreamSink,
    audio_renderer_sink_RemoveStreamSink,
    audio_renderer_sink_GetStreamSinkCount,
    audio_renderer_sink_GetStreamSinkByIndex,
    audio_renderer_sink_GetStreamSinkById,
    audio_renderer_sink_SetPresentationClock,
    audio_renderer_sink_GetPresentationClock,
    audio_renderer_sink_Shutdown,
};

static HRESULT WINAPI audio_renderer_preroll_QueryInterface(IMFMediaSinkPreroll *iface, REFIID riid, void **obj)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSinkPreroll(iface);
    return IMFMediaSink_QueryInterface(&renderer->IMFMediaSink_iface, riid, obj);
}

static ULONG WINAPI audio_renderer_preroll_AddRef(IMFMediaSinkPreroll *iface)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSinkPreroll(iface);
    return IMFMediaSink_AddRef(&renderer->IMFMediaSink_iface);
}

static ULONG WINAPI audio_renderer_preroll_Release(IMFMediaSinkPreroll *iface)
{
    struct audio_renderer *renderer = impl_from_IMFMediaSinkPreroll(iface);
    return IMFMediaSink_Release(&renderer->IMFMediaSink_iface);
}

static HRESULT WINAPI audio_renderer_preroll_NotifyPreroll(IMFMediaSinkPreroll *iface, MFTIME start_time)
{
    FIXME("%p, %s.\n", iface, debugstr_time(start_time));

    return E_NOTIMPL;
}

static const IMFMediaSinkPrerollVtbl audio_renderer_preroll_vtbl =
{
    audio_renderer_preroll_QueryInterface,
    audio_renderer_preroll_AddRef,
    audio_renderer_preroll_Release,
    audio_renderer_preroll_NotifyPreroll,
};

static HRESULT sar_create_object(IMFAttributes *attributes, void *user_context, IUnknown **obj)
{
    struct audio_renderer *renderer;

    TRACE("%p, %p, %p.\n", attributes, user_context, obj);

    if (!(renderer = heap_alloc_zero(sizeof(*renderer))))
        return E_OUTOFMEMORY;

    renderer->IMFMediaSink_iface.lpVtbl = &audio_renderer_sink_vtbl;
    renderer->IMFMediaSinkPreroll_iface.lpVtbl = &audio_renderer_preroll_vtbl;
    renderer->refcount = 1;
    InitializeCriticalSection(&renderer->cs);

    *obj = (IUnknown *)&renderer->IMFMediaSink_iface;

    return S_OK;
}

static void sar_shutdown_object(void *user_context, IUnknown *obj)
{
    /* FIXME: shut down sink */
}

static void sar_free_private(void *user_context)
{
}

static const struct activate_funcs sar_activate_funcs =
{
    sar_create_object,
    sar_shutdown_object,
    sar_free_private,
};

/***********************************************************************
 *      MFCreateAudioRendererActivate (mf.@)
 */
HRESULT WINAPI MFCreateAudioRendererActivate(IMFActivate **activate)
{
    TRACE("%p.\n", activate);

    if (!activate)
        return E_POINTER;

    return create_activation_object(NULL, &sar_activate_funcs, activate);
}

/***********************************************************************
 *      MFCreateAudioRenderer (mf.@)
 */
HRESULT WINAPI MFCreateAudioRenderer(IMFAttributes *attributes, IMFMediaSink **sink)
{
    IUnknown *object;
    HRESULT hr;

    TRACE("%p, %p.\n", attributes, sink);

    if (SUCCEEDED(hr = sar_create_object(attributes, NULL, &object)))
    {
        hr = IUnknown_QueryInterface(object, &IID_IMFMediaSink, (void **)sink);
        IUnknown_Release(object);
    }

    return hr;
}
