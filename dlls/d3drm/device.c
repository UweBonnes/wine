/*
 * Implementation of IDirect3DRMDevice Interface
 *
 * Copyright 2011, 2012 André Hentschel
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

#include "wine/debug.h"

#define COBJMACROS

#include "winbase.h"
#include "wingdi.h"

#include "d3drm_private.h"
#include "initguid.h"
#include "d3drmwin.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3drm);

struct d3drm_device
{
    IDirect3DRMDevice IDirect3DRMDevice_iface;
    IDirect3DRMDevice2 IDirect3DRMDevice2_iface;
    IDirect3DRMDevice3 IDirect3DRMDevice3_iface;
    IDirect3DRMWinDevice IDirect3DRMWinDevice_iface;
    IDirect3DRM *d3drm;
    IDirectDraw *ddraw;
    IDirectDrawSurface *primary_surface, *render_target;
    IDirectDrawClipper *clipper;
    IDirect3DDevice *device;
    LONG ref;
    BOOL dither;
    D3DRMRENDERQUALITY quality;
    DWORD rendermode;
    DWORD height;
    DWORD width;
};

static inline struct d3drm_device *impl_from_IDirect3DRMDevice(IDirect3DRMDevice *iface)
{
    return CONTAINING_RECORD(iface, struct d3drm_device, IDirect3DRMDevice_iface);
}

static inline struct d3drm_device *impl_from_IDirect3DRMDevice2(IDirect3DRMDevice2 *iface)
{
    return CONTAINING_RECORD(iface, struct d3drm_device, IDirect3DRMDevice2_iface);
}

static inline struct d3drm_device *impl_from_IDirect3DRMDevice3(IDirect3DRMDevice3 *iface)
{
    return CONTAINING_RECORD(iface, struct d3drm_device, IDirect3DRMDevice3_iface);
}

IDirect3DRMDevice *IDirect3DRMDevice_from_impl(struct d3drm_device *device)
{
    return &device->IDirect3DRMDevice_iface;
}

IDirect3DRMDevice2 *IDirect3DRMDevice2_from_impl(struct d3drm_device *device)
{
    return &device->IDirect3DRMDevice2_iface;
}

IDirect3DRMDevice3 *IDirect3DRMDevice3_from_impl(struct d3drm_device *device)
{
    return &device->IDirect3DRMDevice3_iface;
}

void d3drm_device_destroy(struct d3drm_device *device)
{
    if (device->device)
    {
        TRACE("Releasing attached ddraw interfaces.\n");
        IDirect3DDevice_Release(device->device);
    }
    if (device->render_target)
        IDirectDrawSurface_Release(device->render_target);
    if (device->primary_surface)
    {
        TRACE("Releasing primary surface and attached clipper.\n");
        IDirectDrawSurface_Release(device->primary_surface);
        IDirectDrawClipper_Release(device->clipper);
    }
    if (device->ddraw)
    {
        IDirectDraw_Release(device->ddraw);
        IDirect3DRM_Release(device->d3drm);
    }
    HeapFree(GetProcessHeap(), 0, device);
}

static inline struct d3drm_device *impl_from_IDirect3DRMWinDevice(IDirect3DRMWinDevice *iface)
{
    return CONTAINING_RECORD(iface, struct d3drm_device, IDirect3DRMWinDevice_iface);
}

HRESULT d3drm_device_create_surfaces_from_clipper(struct d3drm_device *object, IDirectDraw *ddraw, IDirectDrawClipper *clipper, int width, int height, IDirectDrawSurface **surface)
{
    DDSURFACEDESC surface_desc;
    IDirectDrawSurface *primary_surface, *render_target;
    HWND window;
    HRESULT hr;

    hr = IDirectDrawClipper_GetHWnd(clipper, &window);
    if (FAILED(hr))
        return hr;

    hr = IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL);
    if (FAILED(hr))
        return hr;

    memset(&surface_desc, 0, sizeof(surface_desc));
    surface_desc.dwSize = sizeof(surface_desc);
    surface_desc.dwFlags = DDSD_CAPS;
    surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = IDirectDraw_CreateSurface(ddraw, &surface_desc, &primary_surface, NULL);
    if (FAILED(hr))
        return hr;
    hr = IDirectDrawSurface_SetClipper(primary_surface, clipper);
    if (FAILED(hr))
    {
        IDirectDrawSurface_Release(primary_surface);
        return hr;
    }

    memset(&surface_desc, 0, sizeof(surface_desc));
    surface_desc.dwSize = sizeof(surface_desc);
    surface_desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    surface_desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    surface_desc.dwWidth = width;
    surface_desc.dwHeight = height;

    hr = IDirectDraw_CreateSurface(ddraw, &surface_desc, &render_target, NULL);
    if (FAILED(hr))
    {
        IDirectDrawSurface_Release(primary_surface);
        return hr;
    }

    object->primary_surface = primary_surface;
    object->clipper = clipper;
    IDirectDrawClipper_AddRef(clipper);
    *surface = render_target;

    return D3DRM_OK;
}

HRESULT d3drm_device_init(struct d3drm_device *device, UINT version, IDirect3DRM *d3drm, IDirectDraw *ddraw, IDirectDrawSurface *surface)
{
    IDirectDrawSurface *ds = NULL;
    IDirect3DDevice *device1 = NULL;
    IDirect3DDevice2 *device2 = NULL;
    IDirect3D2 *d3d2 = NULL;
    DDSURFACEDESC desc, surface_desc;
    HRESULT hr;

    device->ddraw = ddraw;
    IDirectDraw_AddRef(ddraw);
    device->d3drm = d3drm;
    IDirect3DRM_AddRef(d3drm);
    device->render_target = surface;
    IDirectDrawSurface_AddRef(surface);

    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_GetSurfaceDesc(surface, &desc);
    if (FAILED(hr))
        return hr;

    memset(&surface_desc, 0, sizeof(surface_desc));
    surface_desc.dwSize = sizeof(surface_desc);
    surface_desc.dwFlags = DDSD_CAPS | DDSD_ZBUFFERBITDEPTH | DDSD_WIDTH | DDSD_HEIGHT;
    surface_desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    surface_desc.dwZBufferBitDepth = 16;
    surface_desc.dwWidth = desc.dwWidth;
    surface_desc.dwHeight = desc.dwHeight;
    hr = IDirectDraw_CreateSurface(ddraw, &surface_desc, &ds, NULL);
    if (FAILED(hr))
        return hr;

    hr = IDirectDrawSurface_AddAttachedSurface(surface, ds);
    IDirectDrawSurface_Release(ds);
    if (FAILED(hr))
        return hr;

    if (version == 1)
        hr = IDirectDrawSurface_QueryInterface(surface, &IID_IDirect3DRGBDevice, (void **)&device1);
    else
    {
        IDirectDraw_QueryInterface(ddraw, &IID_IDirect3D2, (void**)&d3d2);
        hr = IDirect3D2_CreateDevice(d3d2, &IID_IDirect3DRGBDevice, surface, &device2);
        IDirect3D2_Release(d3d2);
    }
    if (FAILED(hr))
    {
        IDirectDrawSurface_DeleteAttachedSurface(surface, 0, ds);
        return hr;
    }

    if (version != 1)
    {
        hr = IDirect3DDevice2_QueryInterface(device2, &IID_IDirect3DDevice, (void**)&device1);
        IDirect3DDevice2_Release(device2);
        if (FAILED(hr))
        {
            IDirectDrawSurface_DeleteAttachedSurface(surface, 0, ds);
            return hr;
        }
    }
    device->device = device1;

    return hr;
}

static HRESULT WINAPI d3drm_device1_QueryInterface(IDirect3DRMDevice *iface, REFIID riid, void **out)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    return IDirect3DRMDevice3_QueryInterface(&device->IDirect3DRMDevice3_iface, riid, out);
}

static ULONG WINAPI d3drm_device1_AddRef(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_AddRef(&device->IDirect3DRMDevice3_iface);
}

static ULONG WINAPI d3drm_device1_Release(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_Release(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device1_Clone(IDirect3DRMDevice *iface,
    IUnknown *outer, REFIID iid, void **out)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, outer %p, iid %s, out %p.\n", iface, outer, debugstr_guid(iid), out);

    return IDirect3DRMDevice3_Clone(&device->IDirect3DRMDevice3_iface, outer, iid, out);
}

static HRESULT WINAPI d3drm_device1_AddDestroyCallback(IDirect3DRMDevice *iface,
    D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device1_DeleteDestroyCallback(IDirect3DRMDevice *iface,
    D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device1_SetAppData(IDirect3DRMDevice *iface, DWORD data)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, data %#x.\n", iface, data);

    return IDirect3DRMDevice3_SetAppData(&device->IDirect3DRMDevice3_iface, data);
}

static DWORD WINAPI d3drm_device1_GetAppData(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetAppData(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device1_SetName(IDirect3DRMDevice *iface, const char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    return IDirect3DRMDevice3_SetName(&device->IDirect3DRMDevice3_iface, name);
}

static HRESULT WINAPI d3drm_device1_GetName(IDirect3DRMDevice *iface, DWORD *size, char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, size %p, name %p.\n", iface, size, name);

    return IDirect3DRMDevice3_GetName(&device->IDirect3DRMDevice3_iface, size, name);
}

static HRESULT WINAPI d3drm_device1_GetClassName(IDirect3DRMDevice *iface, DWORD *size, char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, size %p, name %p.\n", iface, size, name);

    return IDirect3DRMDevice3_GetClassName(&device->IDirect3DRMDevice3_iface, size, name);
}

static HRESULT WINAPI d3drm_device1_Init(IDirect3DRMDevice *iface, ULONG width, ULONG height)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, width %u, height %u.\n", iface, width, height);

    return IDirect3DRMDevice3_Init(&device->IDirect3DRMDevice3_iface, width, height);
}

static HRESULT WINAPI d3drm_device1_InitFromD3D(IDirect3DRMDevice *iface,
    IDirect3D *d3d, IDirect3DDevice *d3d_device)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, d3d %p, d3d_device %p.\n", iface, d3d, d3d_device);

    return IDirect3DRMDevice3_InitFromD3D(&device->IDirect3DRMDevice3_iface, d3d, d3d_device);
}

static HRESULT WINAPI d3drm_device1_InitFromClipper(IDirect3DRMDevice *iface,
    IDirectDrawClipper *clipper, GUID *guid, int width, int height)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, clipper %p, guid %s, width %d, height %d.\n",
        iface, clipper, debugstr_guid(guid), width, height);

    return IDirect3DRMDevice3_InitFromClipper(&device->IDirect3DRMDevice3_iface,
        clipper, guid, width, height);
}

static HRESULT WINAPI d3drm_device1_Update(IDirect3DRMDevice *iface)
{
    FIXME("iface %p stub!\n", iface);

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device1_AddUpdateCallback(IDirect3DRMDevice *iface,
    D3DRMUPDATECALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device1_DeleteUpdateCallback(IDirect3DRMDevice *iface,
    D3DRMUPDATECALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device1_SetBufferCount(IDirect3DRMDevice *iface, DWORD count)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, count %u.\n", iface, count);

    return IDirect3DRMDevice3_SetBufferCount(&device->IDirect3DRMDevice3_iface, count);
}

static DWORD WINAPI d3drm_device1_GetBufferCount(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetBufferCount(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device1_SetDither(IDirect3DRMDevice *iface, BOOL enable)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, enabled %#x.\n", iface, enable);

    return IDirect3DRMDevice3_SetDither(&device->IDirect3DRMDevice3_iface, enable);
}

static HRESULT WINAPI d3drm_device1_SetShades(IDirect3DRMDevice *iface, DWORD count)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, count %u.\n", iface, count);

    return IDirect3DRMDevice3_SetShades(&device->IDirect3DRMDevice3_iface, count);
}

static HRESULT WINAPI d3drm_device1_SetQuality(IDirect3DRMDevice *iface, D3DRMRENDERQUALITY quality)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, quality %u.\n", iface, quality);

    return IDirect3DRMDevice3_SetQuality(&device->IDirect3DRMDevice3_iface, quality);
}

static HRESULT WINAPI d3drm_device1_SetTextureQuality(IDirect3DRMDevice *iface, D3DRMTEXTUREQUALITY quality)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, quality %u.\n", iface, quality);

    return IDirect3DRMDevice3_SetTextureQuality(&device->IDirect3DRMDevice3_iface, quality);
}

static HRESULT WINAPI d3drm_device1_GetViewports(IDirect3DRMDevice *iface, IDirect3DRMViewportArray **array)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, array %p.\n", iface, array);

    return IDirect3DRMDevice3_GetViewports(&device->IDirect3DRMDevice3_iface, array);
}

static BOOL WINAPI d3drm_device1_GetDither(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetDither(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device1_GetShades(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetShades(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device1_GetHeight(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetHeight(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device1_GetWidth(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetWidth(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device1_GetTrianglesDrawn(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetTrianglesDrawn(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device1_GetWireframeOptions(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetWireframeOptions(&device->IDirect3DRMDevice3_iface);
}

static D3DRMRENDERQUALITY WINAPI d3drm_device1_GetQuality(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetQuality(&device->IDirect3DRMDevice3_iface);
}

static D3DCOLORMODEL WINAPI d3drm_device1_GetColorModel(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p stub!\n", iface);

    return IDirect3DRMDevice3_GetColorModel(&device->IDirect3DRMDevice3_iface);
}

static D3DRMTEXTUREQUALITY WINAPI d3drm_device1_GetTextureQuality(IDirect3DRMDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetTextureQuality(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device1_GetDirect3DDevice(IDirect3DRMDevice *iface, IDirect3DDevice **d3d_device)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice(iface);

    TRACE("iface %p, d3d_device %p.\n", iface, d3d_device);

    return IDirect3DRMDevice3_GetDirect3DDevice(&device->IDirect3DRMDevice3_iface, d3d_device);
}

static const struct IDirect3DRMDeviceVtbl d3drm_device1_vtbl =
{
    d3drm_device1_QueryInterface,
    d3drm_device1_AddRef,
    d3drm_device1_Release,
    d3drm_device1_Clone,
    d3drm_device1_AddDestroyCallback,
    d3drm_device1_DeleteDestroyCallback,
    d3drm_device1_SetAppData,
    d3drm_device1_GetAppData,
    d3drm_device1_SetName,
    d3drm_device1_GetName,
    d3drm_device1_GetClassName,
    d3drm_device1_Init,
    d3drm_device1_InitFromD3D,
    d3drm_device1_InitFromClipper,
    d3drm_device1_Update,
    d3drm_device1_AddUpdateCallback,
    d3drm_device1_DeleteUpdateCallback,
    d3drm_device1_SetBufferCount,
    d3drm_device1_GetBufferCount,
    d3drm_device1_SetDither,
    d3drm_device1_SetShades,
    d3drm_device1_SetQuality,
    d3drm_device1_SetTextureQuality,
    d3drm_device1_GetViewports,
    d3drm_device1_GetDither,
    d3drm_device1_GetShades,
    d3drm_device1_GetHeight,
    d3drm_device1_GetWidth,
    d3drm_device1_GetTrianglesDrawn,
    d3drm_device1_GetWireframeOptions,
    d3drm_device1_GetQuality,
    d3drm_device1_GetColorModel,
    d3drm_device1_GetTextureQuality,
    d3drm_device1_GetDirect3DDevice,
};

static HRESULT WINAPI d3drm_device2_QueryInterface(IDirect3DRMDevice2 *iface, REFIID riid, void **out)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    return IDirect3DRMDevice3_QueryInterface(&device->IDirect3DRMDevice3_iface, riid, out);
}

static ULONG WINAPI d3drm_device2_AddRef(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_AddRef(&device->IDirect3DRMDevice3_iface);
}

static ULONG WINAPI d3drm_device2_Release(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_Release(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device2_Clone(IDirect3DRMDevice2 *iface,
        IUnknown *outer, REFIID iid, void **out)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, outer %p, iid %s, out %p\n", iface, outer, debugstr_guid(iid), out);

    return IDirect3DRMDevice3_Clone(&device->IDirect3DRMDevice3_iface, outer, iid, out);
}

static HRESULT WINAPI d3drm_device2_AddDestroyCallback(IDirect3DRMDevice2 *iface,
        D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device2_DeleteDestroyCallback(IDirect3DRMDevice2 *iface,
        D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device2_SetAppData(IDirect3DRMDevice2 *iface, DWORD data)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, data %#x.\n", iface, data);

    return IDirect3DRMDevice3_SetAppData(&device->IDirect3DRMDevice3_iface, data);
}

static DWORD WINAPI d3drm_device2_GetAppData(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetAppData(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device2_SetName(IDirect3DRMDevice2 *iface, const char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    return IDirect3DRMDevice3_SetName(&device->IDirect3DRMDevice3_iface, name);
}

static HRESULT WINAPI d3drm_device2_GetName(IDirect3DRMDevice2 *iface, DWORD *size, char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, size %p, name %p.\n", iface, size, name);

    return IDirect3DRMDevice3_GetName(&device->IDirect3DRMDevice3_iface, size, name);
}

static HRESULT WINAPI d3drm_device2_GetClassName(IDirect3DRMDevice2 *iface, DWORD *size, char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, size %p, name %p.\n", iface, size, name);

    return IDirect3DRMDevice3_GetClassName(&device->IDirect3DRMDevice3_iface, size, name);
}

static HRESULT WINAPI d3drm_device2_Init(IDirect3DRMDevice2 *iface, ULONG width, ULONG height)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, width %u, height %u.\n", iface, width, height);

    return IDirect3DRMDevice3_Init(&device->IDirect3DRMDevice3_iface, width, height);
}

static HRESULT WINAPI d3drm_device2_InitFromD3D(IDirect3DRMDevice2 *iface,
        IDirect3D *d3d, IDirect3DDevice *d3d_device)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, d3d %p, d3d_device %p.\n", iface, d3d, d3d_device);

    return IDirect3DRMDevice3_InitFromD3D(&device->IDirect3DRMDevice3_iface, d3d, d3d_device);
}

static HRESULT WINAPI d3drm_device2_InitFromClipper(IDirect3DRMDevice2 *iface,
        IDirectDrawClipper *clipper, GUID *guid, int width, int height)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, clipper %p, guid %s, width %d, height %d.\n",
            iface, clipper, debugstr_guid(guid), width, height);

    return IDirect3DRMDevice3_InitFromClipper(&device->IDirect3DRMDevice3_iface,
            clipper, guid, width, height);
}

static HRESULT WINAPI d3drm_device2_Update(IDirect3DRMDevice2 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device2_AddUpdateCallback(IDirect3DRMDevice2 *iface,
        D3DRMUPDATECALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device2_DeleteUpdateCallback(IDirect3DRMDevice2 *iface,
        D3DRMUPDATECALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device2_SetBufferCount(IDirect3DRMDevice2 *iface, DWORD count)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, count %u.\n", iface, count);

    return IDirect3DRMDevice3_SetBufferCount(&device->IDirect3DRMDevice3_iface, count);
}

static DWORD WINAPI d3drm_device2_GetBufferCount(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetBufferCount(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device2_SetDither(IDirect3DRMDevice2 *iface, BOOL enable)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, enabled %#x.\n", iface, enable);

    return IDirect3DRMDevice3_SetDither(&device->IDirect3DRMDevice3_iface, enable);
}

static HRESULT WINAPI d3drm_device2_SetShades(IDirect3DRMDevice2 *iface, DWORD count)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, count %u.\n", iface, count);

    return IDirect3DRMDevice3_SetShades(&device->IDirect3DRMDevice3_iface, count);
}

static HRESULT WINAPI d3drm_device2_SetQuality(IDirect3DRMDevice2 *iface, D3DRMRENDERQUALITY quality)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, quality %u.\n", iface, quality);

    return IDirect3DRMDevice3_SetQuality(&device->IDirect3DRMDevice3_iface, quality);
}

static HRESULT WINAPI d3drm_device2_SetTextureQuality(IDirect3DRMDevice2 *iface, D3DRMTEXTUREQUALITY quality)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, quality %u.\n", iface, quality);

    return IDirect3DRMDevice3_SetTextureQuality(&device->IDirect3DRMDevice3_iface, quality);
}

static HRESULT WINAPI d3drm_device2_GetViewports(IDirect3DRMDevice2 *iface, IDirect3DRMViewportArray **array)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, array %p.\n", iface, array);

    return IDirect3DRMDevice3_GetViewports(&device->IDirect3DRMDevice3_iface, array);
}

static BOOL WINAPI d3drm_device2_GetDither(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetDither(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device2_GetShades(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetShades(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device2_GetHeight(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetHeight(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device2_GetWidth(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetWidth(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device2_GetTrianglesDrawn(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetTrianglesDrawn(&device->IDirect3DRMDevice3_iface);
}

static DWORD WINAPI d3drm_device2_GetWireframeOptions(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetWireframeOptions(&device->IDirect3DRMDevice3_iface);
}

static D3DRMRENDERQUALITY WINAPI d3drm_device2_GetQuality(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetQuality(&device->IDirect3DRMDevice3_iface);
}

static D3DCOLORMODEL WINAPI d3drm_device2_GetColorModel(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetColorModel(&device->IDirect3DRMDevice3_iface);
}

static D3DRMTEXTUREQUALITY WINAPI d3drm_device2_GetTextureQuality(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetTextureQuality(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device2_GetDirect3DDevice(IDirect3DRMDevice2 *iface, IDirect3DDevice **d3d_device)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, d3d_device %p.\n", iface, d3d_device);

    return IDirect3DRMDevice3_GetDirect3DDevice(&device->IDirect3DRMDevice3_iface, d3d_device);
}

static HRESULT WINAPI d3drm_device2_InitFromD3D2(IDirect3DRMDevice2 *iface,
        IDirect3D2 *d3d, IDirect3DDevice2 *d3d_device)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, d3d %p, d3d_device %p.\n", iface, d3d, d3d_device);

    return IDirect3DRMDevice3_InitFromD3D2(&device->IDirect3DRMDevice3_iface, d3d, d3d_device);
}

static HRESULT WINAPI d3drm_device2_InitFromSurface(IDirect3DRMDevice2 *iface,
        GUID *guid, IDirectDraw *ddraw, IDirectDrawSurface *backbuffer)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, guid %s, ddraw %p, backbuffer %p.\n",
            iface, debugstr_guid(guid), ddraw, backbuffer);

    return IDirect3DRMDevice3_InitFromSurface(&device->IDirect3DRMDevice3_iface, guid, ddraw, backbuffer);
}

static HRESULT WINAPI d3drm_device2_SetRenderMode(IDirect3DRMDevice2 *iface, DWORD flags)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, flags %#x.\n", iface, flags);

    return IDirect3DRMDevice3_SetRenderMode(&device->IDirect3DRMDevice3_iface, flags);
}

static DWORD WINAPI d3drm_device2_GetRenderMode(IDirect3DRMDevice2 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetRenderMode(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device2_GetDirect3DDevice2(IDirect3DRMDevice2 *iface, IDirect3DDevice2 **d3d_device)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice2(iface);

    TRACE("iface %p, d3d_device %p.\n", iface, d3d_device);

    return IDirect3DRMDevice3_GetDirect3DDevice2(&device->IDirect3DRMDevice3_iface, d3d_device);
}

static const struct IDirect3DRMDevice2Vtbl d3drm_device2_vtbl =
{
    d3drm_device2_QueryInterface,
    d3drm_device2_AddRef,
    d3drm_device2_Release,
    d3drm_device2_Clone,
    d3drm_device2_AddDestroyCallback,
    d3drm_device2_DeleteDestroyCallback,
    d3drm_device2_SetAppData,
    d3drm_device2_GetAppData,
    d3drm_device2_SetName,
    d3drm_device2_GetName,
    d3drm_device2_GetClassName,
    d3drm_device2_Init,
    d3drm_device2_InitFromD3D,
    d3drm_device2_InitFromClipper,
    d3drm_device2_Update,
    d3drm_device2_AddUpdateCallback,
    d3drm_device2_DeleteUpdateCallback,
    d3drm_device2_SetBufferCount,
    d3drm_device2_GetBufferCount,
    d3drm_device2_SetDither,
    d3drm_device2_SetShades,
    d3drm_device2_SetQuality,
    d3drm_device2_SetTextureQuality,
    d3drm_device2_GetViewports,
    d3drm_device2_GetDither,
    d3drm_device2_GetShades,
    d3drm_device2_GetHeight,
    d3drm_device2_GetWidth,
    d3drm_device2_GetTrianglesDrawn,
    d3drm_device2_GetWireframeOptions,
    d3drm_device2_GetQuality,
    d3drm_device2_GetColorModel,
    d3drm_device2_GetTextureQuality,
    d3drm_device2_GetDirect3DDevice,
    d3drm_device2_InitFromD3D2,
    d3drm_device2_InitFromSurface,
    d3drm_device2_SetRenderMode,
    d3drm_device2_GetRenderMode,
    d3drm_device2_GetDirect3DDevice2,
};

static HRESULT WINAPI d3drm_device3_QueryInterface(IDirect3DRMDevice3 *iface, REFIID riid, void **out)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualGUID(riid, &IID_IDirect3DRMDevice3)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        *out = &device->IDirect3DRMDevice3_iface;
    }
    else if (IsEqualGUID(riid, &IID_IDirect3DRMDevice2))
    {
        *out = &device->IDirect3DRMDevice2_iface;
    }
    else if (IsEqualGUID(riid, &IID_IDirect3DRMDevice))
    {
        *out = &device->IDirect3DRMDevice_iface;
    }
    else if (IsEqualGUID(riid, &IID_IDirect3DRMWinDevice))
    {
        *out = &device->IDirect3DRMWinDevice_iface;
    }
    else
    {
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI d3drm_device3_AddRef(IDirect3DRMDevice3 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);
    ULONG refcount = InterlockedIncrement(&device->ref);

    TRACE("%p increasing refcount to %u.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI d3drm_device3_Release(IDirect3DRMDevice3 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);
    ULONG refcount = InterlockedDecrement(&device->ref);

    TRACE("%p decreasing refcount to %u.\n", iface, refcount);

    if (!refcount)
        d3drm_device_destroy(device);

    return refcount;
}

static HRESULT WINAPI d3drm_device3_Clone(IDirect3DRMDevice3 *iface,
        IUnknown *outer, REFIID iid, void **out)
{
    FIXME("iface %p, outer %p, iid %s, out %p stub!\n", iface, outer, debugstr_guid(iid), out);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_AddDestroyCallback(IDirect3DRMDevice3 *iface,
        D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_DeleteDestroyCallback(IDirect3DRMDevice3 *iface,
        D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_SetAppData(IDirect3DRMDevice3 *iface, DWORD data)
{
    FIXME("iface %p, data %#x stub!\n", iface, data);

    return E_NOTIMPL;
}

static DWORD WINAPI d3drm_device3_GetAppData(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT WINAPI d3drm_device3_SetName(IDirect3DRMDevice3 *iface, const char *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_a(name));

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_GetName(IDirect3DRMDevice3 *iface, DWORD *size, char *name)
{
    FIXME("iface %p, size %p, name %p stub!\n", iface, size, name);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_GetClassName(IDirect3DRMDevice3 *iface, DWORD *size, char *name)
{
    TRACE("iface %p, size %p, name %p.\n", iface, size, name);

    if (!size || *size < strlen("Device") || !name)
        return E_INVALIDARG;

    strcpy(name, "Device");
    *size = sizeof("Device");

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device3_Init(IDirect3DRMDevice3 *iface, ULONG width, ULONG height)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    FIXME("iface %p, width %u, height %u stub!\n", iface, width, height);

    device->height = height;
    device->width = width;

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device3_InitFromD3D(IDirect3DRMDevice3 *iface,
        IDirect3D *d3d, IDirect3DDevice *d3d_device)
{
    FIXME("iface %p, d3d %p, d3d_device %p stub!\n", iface, d3d, d3d_device);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_InitFromClipper(IDirect3DRMDevice3 *iface,
        IDirectDrawClipper *clipper, GUID *guid, int width, int height)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    FIXME("iface %p, clipper %p, guid %s, width %d, height %d stub!\n",
            iface, clipper, debugstr_guid(guid), width, height);

    device->height = height;
    device->width = width;

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device3_Update(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device3_AddUpdateCallback(IDirect3DRMDevice3 *iface,
        D3DRMUPDATECALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_DeleteUpdateCallback(IDirect3DRMDevice3 *iface,
        D3DRMUPDATECALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_SetBufferCount(IDirect3DRMDevice3 *iface, DWORD count)
{
    FIXME("iface %p, count %u stub!\n", iface, count);

    return E_NOTIMPL;
}

static DWORD WINAPI d3drm_device3_GetBufferCount(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_SetDither(IDirect3DRMDevice3 *iface, BOOL enable)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p, enable %#x.\n", iface, enable);

    device->dither = enable;

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device3_SetShades(IDirect3DRMDevice3 *iface, DWORD count)
{
    FIXME("iface %p, count %u stub!\n", iface, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_SetQuality(IDirect3DRMDevice3 *iface, D3DRMRENDERQUALITY quality)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p, quality %u.\n", iface, quality);

    device->quality = quality;

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device3_SetTextureQuality(IDirect3DRMDevice3 *iface, D3DRMTEXTUREQUALITY quality)
{
    FIXME("iface %p, quality %u stub!\n", iface, quality);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_GetViewports(IDirect3DRMDevice3 *iface, IDirect3DRMViewportArray **array)
{
    FIXME("iface %p, array %p stub!\n", iface, array);

    return E_NOTIMPL;
}

static BOOL WINAPI d3drm_device3_GetDither(IDirect3DRMDevice3 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p.\n", iface);

    return device->dither;
}

static DWORD WINAPI d3drm_device3_GetShades(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static DWORD WINAPI d3drm_device3_GetHeight(IDirect3DRMDevice3 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p.\n", iface);

    return device->height;
}

static DWORD WINAPI d3drm_device3_GetWidth(IDirect3DRMDevice3 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p.\n", iface);

    return device->width;
}

static DWORD WINAPI d3drm_device3_GetTrianglesDrawn(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static DWORD WINAPI d3drm_device3_GetWireframeOptions(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static D3DRMRENDERQUALITY WINAPI d3drm_device3_GetQuality(IDirect3DRMDevice3 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p.\n", iface);

    return device->quality;
}

static D3DCOLORMODEL WINAPI d3drm_device3_GetColorModel(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static D3DRMTEXTUREQUALITY WINAPI d3drm_device3_GetTextureQuality(IDirect3DRMDevice3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_GetDirect3DDevice(IDirect3DRMDevice3 *iface, IDirect3DDevice **d3d_device)
{
    FIXME("iface %p, d3d_device %p stub!\n", iface, d3d_device);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_InitFromD3D2(IDirect3DRMDevice3 *iface,
        IDirect3D2 *d3d, IDirect3DDevice2 *d3d_device)
{
    FIXME("iface %p, d3d %p, d3d_device %p stub!\n", iface, d3d, d3d_device);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_InitFromSurface(IDirect3DRMDevice3 *iface,
        GUID *guid, IDirectDraw *ddraw, IDirectDrawSurface *backbuffer)
{
    FIXME("iface %p, guid %s, ddraw %p, backbuffer %p stub!\n",
            iface, debugstr_guid(guid), ddraw, backbuffer);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_SetRenderMode(IDirect3DRMDevice3 *iface, DWORD flags)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p, flags %#x.\n", iface, flags);

    device->rendermode = flags;

    return D3DRM_OK;
}

static DWORD WINAPI d3drm_device3_GetRenderMode(IDirect3DRMDevice3 *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMDevice3(iface);

    TRACE("iface %p.\n", iface);

    return device->rendermode;
}

static HRESULT WINAPI d3drm_device3_GetDirect3DDevice2(IDirect3DRMDevice3 *iface, IDirect3DDevice2 **d3d_device)
{
    FIXME("iface %p, d3d_device %p stub!\n", iface, d3d_device);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_FindPreferredTextureFormat(IDirect3DRMDevice3 *iface,
        DWORD bitdepths, DWORD flags, DDPIXELFORMAT *pf)
{
    FIXME("iface %p, bitdepths %u, flags %#x, pf %p stub!\n", iface, bitdepths, flags, pf);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_RenderStateChange(IDirect3DRMDevice3 *iface,
        D3DRENDERSTATETYPE state, DWORD value, DWORD flags)
{
    FIXME("iface %p, state %#x, value %#x, flags %#x stub!\n", iface, state, value, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_LightStateChange(IDirect3DRMDevice3 *iface,
        D3DLIGHTSTATETYPE state, DWORD value, DWORD flags)
{
    FIXME("iface %p, state %#x, value %#x, flags %#x stub!\n", iface, state, value, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_GetStateChangeOptions(IDirect3DRMDevice3 *iface,
        DWORD state_class, DWORD state_idx, DWORD *flags)
{
    FIXME("iface %p, state_class %#x, state_idx %#x, flags %p stub!\n",
            iface, state_class, state_idx, flags);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device3_SetStateChangeOptions(IDirect3DRMDevice3 *iface,
        DWORD state_class, DWORD state_idx, DWORD flags)
{
    FIXME("iface %p, state_class %#x, state_idx %#x, flags %#x stub!\n",
            iface, state_class, state_idx, flags);

    return E_NOTIMPL;
}

static const struct IDirect3DRMDevice3Vtbl d3drm_device3_vtbl =
{
    d3drm_device3_QueryInterface,
    d3drm_device3_AddRef,
    d3drm_device3_Release,
    d3drm_device3_Clone,
    d3drm_device3_AddDestroyCallback,
    d3drm_device3_DeleteDestroyCallback,
    d3drm_device3_SetAppData,
    d3drm_device3_GetAppData,
    d3drm_device3_SetName,
    d3drm_device3_GetName,
    d3drm_device3_GetClassName,
    d3drm_device3_Init,
    d3drm_device3_InitFromD3D,
    d3drm_device3_InitFromClipper,
    d3drm_device3_Update,
    d3drm_device3_AddUpdateCallback,
    d3drm_device3_DeleteUpdateCallback,
    d3drm_device3_SetBufferCount,
    d3drm_device3_GetBufferCount,
    d3drm_device3_SetDither,
    d3drm_device3_SetShades,
    d3drm_device3_SetQuality,
    d3drm_device3_SetTextureQuality,
    d3drm_device3_GetViewports,
    d3drm_device3_GetDither,
    d3drm_device3_GetShades,
    d3drm_device3_GetHeight,
    d3drm_device3_GetWidth,
    d3drm_device3_GetTrianglesDrawn,
    d3drm_device3_GetWireframeOptions,
    d3drm_device3_GetQuality,
    d3drm_device3_GetColorModel,
    d3drm_device3_GetTextureQuality,
    d3drm_device3_GetDirect3DDevice,
    d3drm_device3_InitFromD3D2,
    d3drm_device3_InitFromSurface,
    d3drm_device3_SetRenderMode,
    d3drm_device3_GetRenderMode,
    d3drm_device3_GetDirect3DDevice2,
    d3drm_device3_FindPreferredTextureFormat,
    d3drm_device3_RenderStateChange,
    d3drm_device3_LightStateChange,
    d3drm_device3_GetStateChangeOptions,
    d3drm_device3_SetStateChangeOptions,
};

static HRESULT WINAPI d3drm_device_win_QueryInterface(IDirect3DRMWinDevice *iface, REFIID riid, void **out)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    return d3drm_device3_QueryInterface(&device->IDirect3DRMDevice3_iface, riid, out);
}

static ULONG WINAPI d3drm_device_win_AddRef(IDirect3DRMWinDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p.\n", iface);

    return d3drm_device3_AddRef(&device->IDirect3DRMDevice3_iface);
}

static ULONG WINAPI d3drm_device_win_Release(IDirect3DRMWinDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p.\n", iface);

    return d3drm_device3_Release(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device_win_Clone(IDirect3DRMWinDevice *iface,
        IUnknown *outer, REFIID iid, void **out)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p, outer %p, iid %s, out %p\n", iface, outer, debugstr_guid(iid), out);

    return IDirect3DRMDevice3_Clone(&device->IDirect3DRMDevice3_iface, outer, iid, out);
}

static HRESULT WINAPI d3drm_device_win_AddDestroyCallback(IDirect3DRMWinDevice *iface,
        D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device_win_DeleteDestroyCallback(IDirect3DRMWinDevice *iface,
        D3DRMOBJECTCALLBACK cb, void *ctx)
{
    FIXME("iface %p, cb %p, ctx %p stub!\n", iface, cb, ctx);

    return E_NOTIMPL;
}

static HRESULT WINAPI d3drm_device_win_SetAppData(IDirect3DRMWinDevice *iface, DWORD data)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p, data %#x.\n", iface, data);

    return IDirect3DRMDevice3_SetAppData(&device->IDirect3DRMDevice3_iface, data);
}

static DWORD WINAPI d3drm_device_win_GetAppData(IDirect3DRMWinDevice *iface)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p.\n", iface);

    return IDirect3DRMDevice3_GetAppData(&device->IDirect3DRMDevice3_iface);
}

static HRESULT WINAPI d3drm_device_win_SetName(IDirect3DRMWinDevice *iface, const char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    return IDirect3DRMDevice3_SetName(&device->IDirect3DRMDevice3_iface, name);
}

static HRESULT WINAPI d3drm_device_win_GetName(IDirect3DRMWinDevice *iface, DWORD *size, char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p, size %p, name %p stub!\n", iface, size, name);

    return IDirect3DRMDevice3_GetName(&device->IDirect3DRMDevice3_iface, size, name);
}

static HRESULT WINAPI d3drm_device_win_GetClassName(IDirect3DRMWinDevice *iface, DWORD *size, char *name)
{
    struct d3drm_device *device = impl_from_IDirect3DRMWinDevice(iface);

    TRACE("iface %p, size %p, name %p.\n", iface, size, name);

    return IDirect3DRMDevice3_GetClassName(&device->IDirect3DRMDevice3_iface, size, name);
}

static HRESULT WINAPI d3drm_device_win_HandlePaint(IDirect3DRMWinDevice *iface, HDC dc)
{
    FIXME("iface %p, dc %p stub!\n", iface, dc);

    return D3DRM_OK;
}

static HRESULT WINAPI d3drm_device_win_HandleActivate(IDirect3DRMWinDevice *iface, WORD wparam)
{
    FIXME("iface %p, wparam %#x stub!\n", iface, wparam);

    return D3DRM_OK;
}

static const struct IDirect3DRMWinDeviceVtbl d3drm_device_win_vtbl =
{
    d3drm_device_win_QueryInterface,
    d3drm_device_win_AddRef,
    d3drm_device_win_Release,
    d3drm_device_win_Clone,
    d3drm_device_win_AddDestroyCallback,
    d3drm_device_win_DeleteDestroyCallback,
    d3drm_device_win_SetAppData,
    d3drm_device_win_GetAppData,
    d3drm_device_win_SetName,
    d3drm_device_win_GetName,
    d3drm_device_win_GetClassName,
    d3drm_device_win_HandlePaint,
    d3drm_device_win_HandleActivate,
};

HRESULT d3drm_device_create(struct d3drm_device **out)
{
    struct d3drm_device *object;

    TRACE("out %p.\n", out);

    if (!(object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IDirect3DRMDevice_iface.lpVtbl = &d3drm_device1_vtbl;
    object->IDirect3DRMDevice2_iface.lpVtbl = &d3drm_device2_vtbl;
    object->IDirect3DRMDevice3_iface.lpVtbl = &d3drm_device3_vtbl;
    object->IDirect3DRMWinDevice_iface.lpVtbl = &d3drm_device_win_vtbl;
    object->ref = 1;

    *out = object;

    return D3DRM_OK;
}
