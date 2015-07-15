/*
 * Copyright 2015 Henri Verbeet for CodeWeavers
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

#include "config.h"
#include "wine/port.h"

#include "d2d1_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

static inline struct d2d_state_block *impl_from_ID2D1DrawingStateBlock(ID2D1DrawingStateBlock *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_state_block, ID2D1DrawingStateBlock_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_state_block_QueryInterface(ID2D1DrawingStateBlock *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1DrawingStateBlock)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1DrawingStateBlock_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_state_block_AddRef(ID2D1DrawingStateBlock *iface)
{
    struct d2d_state_block *state_block = impl_from_ID2D1DrawingStateBlock(iface);
    ULONG refcount = InterlockedIncrement(&state_block->refcount);

    TRACE("%p increasing refcount to %u.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_state_block_Release(ID2D1DrawingStateBlock *iface)
{
    struct d2d_state_block *state_block = impl_from_ID2D1DrawingStateBlock(iface);
    ULONG refcount = InterlockedDecrement(&state_block->refcount);

    TRACE("%p decreasing refcount to %u.\n", iface, refcount);

    if (!refcount)
        HeapFree(GetProcessHeap(), 0, state_block);

    return refcount;
}

static void STDMETHODCALLTYPE d2d_state_block_GetFactory(ID2D1DrawingStateBlock *iface, ID2D1Factory **factory)
{
    FIXME("iface %p, factory %p stub!\n", iface, factory);

    *factory = NULL;
}

static void STDMETHODCALLTYPE d2d_state_block_GetDescription(ID2D1DrawingStateBlock *iface,
        D2D1_DRAWING_STATE_DESCRIPTION *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);
}

static void STDMETHODCALLTYPE d2d_state_block_SetDescription(ID2D1DrawingStateBlock *iface,
        const D2D1_DRAWING_STATE_DESCRIPTION *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);
}

static void STDMETHODCALLTYPE d2d_state_block_SetTextRenderingParams(ID2D1DrawingStateBlock *iface,
        IDWriteRenderingParams *text_rendering_params)
{
    FIXME("iface %p, text_rendering_params %p stub!\n", iface, text_rendering_params);
}

static void STDMETHODCALLTYPE d2d_state_block_GetTextRenderingParams(ID2D1DrawingStateBlock *iface,
        IDWriteRenderingParams **text_rendering_params)
{
    FIXME("iface %p, text_rendering_params %p stub!\n", iface, text_rendering_params);
}

static const struct ID2D1DrawingStateBlockVtbl d2d_state_block_vtbl =
{
    d2d_state_block_QueryInterface,
    d2d_state_block_AddRef,
    d2d_state_block_Release,
    d2d_state_block_GetFactory,
    d2d_state_block_GetDescription,
    d2d_state_block_SetDescription,
    d2d_state_block_SetTextRenderingParams,
    d2d_state_block_GetTextRenderingParams,
};

void d2d_state_block_init(struct d2d_state_block *state_block, const D2D1_DRAWING_STATE_DESCRIPTION *desc,
        IDWriteRenderingParams *text_rendering_params)
{
    FIXME("Ignoring state block properties.\n");

    state_block->ID2D1DrawingStateBlock_iface.lpVtbl = &d2d_state_block_vtbl;
    state_block->refcount = 1;
}
