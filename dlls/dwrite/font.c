/*
 *    Font and collections
 *
 * Copyright 2012 Nikolay Sivov for CodeWeavers
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

#include "dwrite.h"
#include "dwrite_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwrite);

/* PANOSE is 10 bytes in size, need to pack the structure properly */
#include "pshpack2.h"
typedef struct
{
    ULONG version;
    ULONG revision;
    ULONG checksumadj;
    ULONG magic;
    USHORT flags;
    USHORT unitsPerEm;
    ULONGLONG created;
    ULONGLONG modified;
    SHORT xMin;
    SHORT yMin;
    SHORT xMax;
    SHORT yMax;
    USHORT macStyle;
    USHORT lowestRecPPEM;
    SHORT direction_hint;
    SHORT index_format;
    SHORT glyphdata_format;
} TT_HEAD;

typedef struct
{
    ULONG Version;
    ULONG italicAngle;
    SHORT underlinePosition;
    SHORT underlineThickness;
    ULONG fixed_pitch;
    ULONG minmemType42;
    ULONG maxmemType42;
    ULONG minmemType1;
    ULONG maxmemType1;
} TT_POST;

typedef struct
{
    USHORT version;
    SHORT xAvgCharWidth;
    USHORT usWeightClass;
    USHORT usWidthClass;
    SHORT fsType;
    SHORT ySubscriptXSize;
    SHORT ySubscriptYSize;
    SHORT ySubscriptXOffset;
    SHORT ySubscriptYOffset;
    SHORT ySuperscriptXSize;
    SHORT ySuperscriptYSize;
    SHORT ySuperscriptXOffset;
    SHORT ySuperscriptYOffset;
    SHORT yStrikeoutSize;
    SHORT yStrikeoutPosition;
    SHORT sFamilyClass;
    PANOSE panose;
    ULONG ulUnicodeRange1;
    ULONG ulUnicodeRange2;
    ULONG ulUnicodeRange3;
    ULONG ulUnicodeRange4;
    CHAR achVendID[4];
    USHORT fsSelection;
    USHORT usFirstCharIndex;
    USHORT usLastCharIndex;
    /* According to the Apple spec, original version didn't have the below fields,
     * version numbers were taken from the OpenType spec.
     */
    /* version 0 (TrueType 1.5) */
    USHORT sTypoAscender;
    USHORT sTypoDescender;
    USHORT sTypoLineGap;
    USHORT usWinAscent;
    USHORT usWinDescent;
    /* version 1 (TrueType 1.66) */
    ULONG ulCodePageRange1;
    ULONG ulCodePageRange2;
    /* version 2 (OpenType 1.2) */
    SHORT sxHeight;
    SHORT sCapHeight;
    USHORT usDefaultChar;
    USHORT usBreakChar;
    USHORT usMaxContext;
} TT_OS2_V2;
#include "poppack.h"

#ifdef WORDS_BIGENDIAN
#define GET_BE_WORD(x) (x)
#define GET_BE_DWORD(x) (x)
#else
#define GET_BE_WORD(x) MAKEWORD(HIBYTE(x), LOBYTE(x))
#define GET_BE_DWORD(x) MAKELONG(GET_BE_WORD(HIWORD(x)), GET_BE_WORD(LOWORD(x)));
#endif

#define MS_MAKE_TAG(ch0, ch1, ch2, ch3) \
                    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
                    ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))

#define MS_HEAD_TAG MS_MAKE_TAG('h','e','a','d')
#define MS_OS2_TAG  MS_MAKE_TAG('O','S','/','2')
#define MS_POST_TAG MS_MAKE_TAG('p','o','s','t')
#define MS_CMAP_TAG MS_MAKE_TAG('c','m','a','p')

struct dwrite_fontface_data {
    LONG ref;

    DWRITE_FONT_FACE_TYPE type;
    UINT32 file_count;
    IDWriteFontFile ** files;
    DWRITE_FONT_SIMULATIONS simulations;
    UINT32 index;
};

struct dwrite_font_data {
    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_SIMULATIONS simulations;
    DWRITE_FONT_METRICS metrics;

    struct dwrite_fontface_data *face_data;

    WCHAR *facename;
};

struct dwrite_fontcollection {
    IDWriteFontCollection IDWriteFontCollection_iface;
    LONG ref;

    WCHAR **families;
    UINT32 count;
    int alloc;
};

struct dwrite_fontfamily {
    IDWriteFontFamily IDWriteFontFamily_iface;
    LONG ref;

    WCHAR *familyname;
};

struct dwrite_font {
    IDWriteFont IDWriteFont_iface;
    LONG ref;

    BOOL is_system;
    IDWriteFontFamily *family;
    IDWriteFontFace *face;

    struct dwrite_font_data *data;
};

#define DWRITE_FONTTABLE_MAGIC 0xededfafa

struct dwrite_fonttable {
    UINT32 magic;
    LPVOID context;
    UINT32 file_index;
};

struct dwrite_fontface {
    IDWriteFontFace IDWriteFontFace_iface;
    LONG ref;

    struct dwrite_fontface_data *data;

    LPVOID CMAP_table;
    LPVOID CMAP_context;
    DWORD CMAP_size;

    BOOL is_system;
    LOGFONTW logfont;
};

struct dwrite_fontfile {
    IDWriteFontFile IDWriteFontFile_iface;
    LONG ref;

    IDWriteFontFileLoader *loader;
    void *reference_key;
    UINT32 key_size;
    IDWriteFontFileStream *stream;
};

static HRESULT create_fontfamily(const WCHAR *familyname, IDWriteFontFamily **family);
static HRESULT create_font_base(IDWriteFont **font);

static inline struct dwrite_fontface *impl_from_IDWriteFontFace(IDWriteFontFace *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontface, IDWriteFontFace_iface);
}

static inline struct dwrite_font *impl_from_IDWriteFont(IDWriteFont *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_font, IDWriteFont_iface);
}

static inline struct dwrite_fontfile *impl_from_IDWriteFontFile(IDWriteFontFile *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontfile, IDWriteFontFile_iface);
}

static inline struct dwrite_fontfamily *impl_from_IDWriteFontFamily(IDWriteFontFamily *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontfamily, IDWriteFontFamily_iface);
}

static inline struct dwrite_fontcollection *impl_from_IDWriteFontCollection(IDWriteFontCollection *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontcollection, IDWriteFontCollection_iface);
}

static HRESULT _dwritefontfile_GetFontFileStream(IDWriteFontFile *iface, IDWriteFontFileStream **stream)
{
    HRESULT hr;
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    if (!This->stream)
    {
        hr = IDWriteFontFileLoader_CreateStreamFromKey(This->loader, This->reference_key, This->key_size, &This->stream);
        if (FAILED(hr))
            return hr;
    }
    if (This->stream)
    {
        IDWriteFontFileStream_AddRef(This->stream);
        *stream = This->stream;
        return S_OK;
    }
    return E_FAIL;
}

static VOID _free_fontface_data(struct dwrite_fontface_data *data)
{
    int i;
    if (!data)
        return;
    i = InterlockedDecrement(&data->ref);
    if (i > 0)
        return;
    for (i = 0; i < data->file_count; i++)
        IDWriteFontFile_Release(data->files[i]);
    heap_free(data->files);
    heap_free(data);
}

static HRESULT WINAPI dwritefontface_QueryInterface(IDWriteFontFace *iface, REFIID riid, void **obj)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFace))
    {
        *obj = iface;
        IDWriteFontFace_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontface_AddRef(IDWriteFontFace *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontface_Release(IDWriteFontFace *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        if (This->CMAP_context)
            IDWriteFontFace_ReleaseFontTable(iface, This->CMAP_context);
        _free_fontface_data(This->data);
        heap_free(This);
    }

    return ref;
}

static DWRITE_FONT_FACE_TYPE WINAPI dwritefontface_GetType(IDWriteFontFace *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    TRACE("(%p)\n", This);
    return This->data->type;
}

static HRESULT WINAPI dwritefontface_GetFiles(IDWriteFontFace *iface, UINT32 *number_of_files,
    IDWriteFontFile **fontfiles)
{
    int i;
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    TRACE("(%p)->(%p %p)\n", This, number_of_files, fontfiles);
    if (fontfiles == NULL)
    {
        *number_of_files = This->data->file_count;
        return S_OK;
    }
    if (*number_of_files < This->data->file_count)
        return E_INVALIDARG;

    for (i = 0; i < This->data->file_count; i++)
    {
        IDWriteFontFile_AddRef(This->data->files[i]);
        fontfiles[i] = This->data->files[i];
    }

    return S_OK;
}

static UINT32 WINAPI dwritefontface_GetIndex(IDWriteFontFace *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    TRACE("(%p)\n", This);
    return This->data->index;
}

static DWRITE_FONT_SIMULATIONS WINAPI dwritefontface_GetSimulations(IDWriteFontFace *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    TRACE("(%p)\n", This);
    return This->data->simulations;
}

static BOOL WINAPI dwritefontface_IsSymbolFont(IDWriteFontFace *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p): stub\n", This);
    return FALSE;
}

static void WINAPI dwritefontface_GetMetrics(IDWriteFontFace *iface, DWRITE_FONT_METRICS *metrics)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p)->(%p): stub\n", This, metrics);
}

static UINT16 WINAPI dwritefontface_GetGlyphCount(IDWriteFontFace *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p): stub\n", This);
    return 0;
}

static HRESULT WINAPI dwritefontface_GetDesignGlyphMetrics(IDWriteFontFace *iface,
    UINT16 const *glyph_indices, UINT32 glyph_count, DWRITE_GLYPH_METRICS *metrics, BOOL is_sideways)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p)->(%p %u %p %d): stub\n", This, glyph_indices, glyph_count, metrics, is_sideways);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface_GetGlyphIndices(IDWriteFontFace *iface, UINT32 const *codepoints,
    UINT32 count, UINT16 *glyph_indices)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    unsigned int i;

    if (This->is_system)
    {
        HFONT hfont;
        WCHAR *str;
        HDC hdc;

        TRACE("(%p)->(%p %u %p)\n", This, codepoints, count, glyph_indices);

        str = heap_alloc(count*sizeof(WCHAR));
        if (!str) return E_OUTOFMEMORY;

        for (i = 0; i < count; i++)
            str[i] = codepoints[i] < 0x10000 ? codepoints[i] : '?';

        hdc = CreateCompatibleDC(0);
        hfont = CreateFontIndirectW(&This->logfont);
        SelectObject(hdc, hfont);

        GetGlyphIndicesW(hdc, str, count, glyph_indices, 0);
        heap_free(str);

        DeleteDC(hdc);
        DeleteObject(hfont);

        return S_OK;
    }
    else
    {
        HRESULT hr;
        TRACE("(%p)->(%p %u %p)\n", This, codepoints, count, glyph_indices);
        if (!This->CMAP_table)
        {
            BOOL exists = FALSE;
            hr = IDWriteFontFace_TryGetFontTable(iface, MS_CMAP_TAG, (const void**)&This->CMAP_table, &This->CMAP_size, &This->CMAP_context, &exists);
            if (FAILED(hr) || !exists)
            {
                ERR("Font does not have a CMAP table\n");
                return E_FAIL;
            }
        }

        for (i = 0; i < count; i++)
        {
            OpenType_CMAP_GetGlyphIndex(This->CMAP_table, codepoints[i], &glyph_indices[i], 0);
        }
        return S_OK;
    }
}

static HRESULT WINAPI dwritefontface_TryGetFontTable(IDWriteFontFace *iface, UINT32 table_tag,
    const void **table_data, UINT32 *table_size, void **context, BOOL *exists)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    if (This->is_system)
    {
        FIXME("(%p)->(%u %p %p %p %p): stub\n", This, table_tag, table_data, table_size, context, exists);
        return E_NOTIMPL;
    }
    else
    {
        HRESULT hr = S_OK;
        int i;
        struct dwrite_fonttable *table;

        TRACE("(%p)->(%u %p %p %p %p)\n", This, table_tag, table_data, table_size, context, exists);

        table = heap_alloc(sizeof(struct dwrite_fonttable));
        table->magic = DWRITE_FONTTABLE_MAGIC;
        if (!table)
            return E_OUTOFMEMORY;

        *exists = FALSE;
        for (i = 0; i < This->data->file_count && !(*exists); i++)
        {
            IDWriteFontFileStream *stream;
            hr = _dwritefontfile_GetFontFileStream(This->data->files[i], &stream);
            if (FAILED(hr))
                continue;
            table->file_index = i;

            hr = find_font_table(stream, This->data->index, table_tag, table_data, &table->context, table_size, exists);

            IDWriteFontFileStream_Release(stream);
        }
        if (FAILED(hr) && !*exists)
            heap_free(table);
        else
            *context = (LPVOID)table;
        return hr;
    }
}

static void WINAPI dwritefontface_ReleaseFontTable(IDWriteFontFace *iface, void *table_context)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    struct dwrite_fonttable *table = (struct dwrite_fonttable *)table_context;
    IDWriteFontFileStream *stream;
    HRESULT hr;
    TRACE("(%p)->(%p)\n", This, table_context);

    if (table->magic != DWRITE_FONTTABLE_MAGIC)
    {
        TRACE("Invalid table magic\n");
        return;
    }

    hr = _dwritefontfile_GetFontFileStream(This->data->files[table->file_index], &stream);
    if (FAILED(hr))
        return;
    IDWriteFontFileStream_ReleaseFileFragment(stream, table->context);
    IDWriteFontFileStream_Release(stream);
    heap_free(table);
}

static HRESULT WINAPI dwritefontface_GetGlyphRunOutline(IDWriteFontFace *iface, FLOAT emSize,
    UINT16 const *glyph_indices, FLOAT const* glyph_advances, DWRITE_GLYPH_OFFSET const *glyph_offsets,
    UINT32 glyph_count, BOOL is_sideways, BOOL is_rtl, IDWriteGeometrySink *geometrysink)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p)->(%f %p %p %p %u %d %d %p): stub\n", This, emSize, glyph_indices, glyph_advances, glyph_offsets,
        glyph_count, is_sideways, is_rtl, geometrysink);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface_GetRecommendedRenderingMode(IDWriteFontFace *iface, FLOAT emSize,
    FLOAT pixels_per_dip, DWRITE_MEASURING_MODE mode, IDWriteRenderingParams* params, DWRITE_RENDERING_MODE* rendering_mode)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p)->(%f %f %d %p %p): stub\n", This, emSize, pixels_per_dip, mode, params, rendering_mode);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface_GetGdiCompatibleMetrics(IDWriteFontFace *iface, FLOAT emSize, FLOAT pixels_per_dip,
    DWRITE_MATRIX const *transform, DWRITE_FONT_METRICS *metrics)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p)->(%f %f %p %p): stub\n", This, emSize, pixels_per_dip, transform, metrics);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface_GetGdiCompatibleGlyphMetrics(IDWriteFontFace *iface, FLOAT emSize, FLOAT pixels_per_dip,
    DWRITE_MATRIX const *transform, BOOL use_gdi_natural, UINT16 const *glyph_indices, UINT32 glyph_count,
    DWRITE_GLYPH_METRICS *metrics, BOOL is_sideways)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace(iface);
    FIXME("(%p)->(%f %f %p %d %p %u %p %d): stub\n", This, emSize, pixels_per_dip, transform, use_gdi_natural, glyph_indices,
        glyph_count, metrics, is_sideways);
    return E_NOTIMPL;
}

static const IDWriteFontFaceVtbl dwritefontfacevtbl = {
    dwritefontface_QueryInterface,
    dwritefontface_AddRef,
    dwritefontface_Release,
    dwritefontface_GetType,
    dwritefontface_GetFiles,
    dwritefontface_GetIndex,
    dwritefontface_GetSimulations,
    dwritefontface_IsSymbolFont,
    dwritefontface_GetMetrics,
    dwritefontface_GetGlyphCount,
    dwritefontface_GetDesignGlyphMetrics,
    dwritefontface_GetGlyphIndices,
    dwritefontface_TryGetFontTable,
    dwritefontface_ReleaseFontTable,
    dwritefontface_GetGlyphRunOutline,
    dwritefontface_GetRecommendedRenderingMode,
    dwritefontface_GetGdiCompatibleMetrics,
    dwritefontface_GetGdiCompatibleGlyphMetrics
};

static HRESULT create_system_fontface(struct dwrite_font *font, IDWriteFontFace **face)
{
    struct dwrite_fontface *This;

    *face = NULL;

    This = heap_alloc(sizeof(struct dwrite_fontface));
    if (!This) return E_OUTOFMEMORY;
    This->data = heap_alloc(sizeof(struct dwrite_fontface_data));
    if (!This->data)
    {
        heap_free(This);
        return E_OUTOFMEMORY;
    }

    This->IDWriteFontFace_iface.lpVtbl = &dwritefontfacevtbl;
    This->ref = 1;
    This->data->type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    This->data->file_count = 0;
    This->data->files = NULL;
    This->data->index = 0;
    This->data->simulations = DWRITE_FONT_SIMULATIONS_NONE;
    This->CMAP_table = NULL;
    This->CMAP_context = NULL;
    This->CMAP_size = 0;

    This->is_system = TRUE;
    memset(&This->logfont, 0, sizeof(This->logfont));
    This->logfont.lfItalic = font->data->style == DWRITE_FONT_STYLE_ITALIC;
    /* weight values from DWRITE_FONT_WEIGHT match values used for LOGFONT */
    This->logfont.lfWeight = font->data->weight;
    strcpyW(This->logfont.lfFaceName, font->data->facename);

    *face = &This->IDWriteFontFace_iface;

    return S_OK;
}

HRESULT convert_fontface_to_logfont(IDWriteFontFace *face, LOGFONTW *logfont)
{
    struct dwrite_fontface *fontface = impl_from_IDWriteFontFace(face);

    *logfont = fontface->logfont;

    return S_OK;
}

static HRESULT WINAPI dwritefont_QueryInterface(IDWriteFont *iface, REFIID riid, void **obj)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFont))
    {
        *obj = iface;
        IDWriteFont_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefont_AddRef(IDWriteFont *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefont_Release(IDWriteFont *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        if (This->face) IDWriteFontFace_Release(This->face);
        IDWriteFontFamily_Release(This->family);
        heap_free(This->data->facename);
        _free_fontface_data(This->data->face_data);
        heap_free(This->data);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritefont_GetFontFamily(IDWriteFont *iface, IDWriteFontFamily **family)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    TRACE("(%p)->(%p)\n", This, family);

    *family = This->family;
    IDWriteFontFamily_AddRef(*family);
    return S_OK;
}

static DWRITE_FONT_WEIGHT WINAPI dwritefont_GetWeight(IDWriteFont *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    TRACE("(%p)\n", This);
    return This->data->weight;
}

static DWRITE_FONT_STRETCH WINAPI dwritefont_GetStretch(IDWriteFont *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    TRACE("(%p)\n", This);
    return This->data->stretch;
}

static DWRITE_FONT_STYLE WINAPI dwritefont_GetStyle(IDWriteFont *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    TRACE("(%p)\n", This);
    return This->data->style;
}

static BOOL WINAPI dwritefont_IsSymbolFont(IDWriteFont *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    FIXME("(%p): stub\n", This);
    return FALSE;
}

static HRESULT WINAPI dwritefont_GetFaceNames(IDWriteFont *iface, IDWriteLocalizedStrings **names)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    FIXME("(%p)->(%p): stub\n", This, names);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefont_GetInformationalStrings(IDWriteFont *iface,
    DWRITE_INFORMATIONAL_STRING_ID stringid, IDWriteLocalizedStrings **strings, BOOL *exists)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    FIXME("(%p)->(%d %p %p): stub\n", This, stringid, strings, exists);
    return E_NOTIMPL;
}

static DWRITE_FONT_SIMULATIONS WINAPI dwritefont_GetSimulations(IDWriteFont *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    TRACE("(%p)\n", This);
    return This->data->simulations;
}

static void WINAPI dwritefont_GetMetrics(IDWriteFont *iface, DWRITE_FONT_METRICS *metrics)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);

    TRACE("(%p)->(%p)\n", This, metrics);
    *metrics = This->data->metrics;
}

static HRESULT WINAPI dwritefont_HasCharacter(IDWriteFont *iface, UINT32 value, BOOL *exists)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);
    FIXME("(%p)->(0x%08x %p): stub\n", This, value, exists);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefont_CreateFontFace(IDWriteFont *iface, IDWriteFontFace **face)
{
    struct dwrite_font *This = impl_from_IDWriteFont(iface);

    if (This->is_system)
    {
        TRACE("(%p)->(%p)\n", This, face);

        if (!This->face)
        {
            HRESULT hr = create_system_fontface(This, &This->face);
            if (FAILED(hr)) return hr;
        }

        *face = This->face;
        IDWriteFontFace_AddRef(*face);

        return S_OK;
    }
    else
    {
        TRACE("(%p)->(%p)\n", This, face);

        if (!This->face)
        {
            HRESULT hr = font_create_fontface(NULL, This->data->face_data->type, This->data->face_data->file_count, This->data->face_data->files, This->data->face_data->index, This->data->face_data->simulations, &This->face);
            if (FAILED(hr)) return hr;
        }

        *face = This->face;
        IDWriteFontFace_AddRef(*face);

        return S_OK;
    }
}

static const IDWriteFontVtbl dwritefontvtbl = {
    dwritefont_QueryInterface,
    dwritefont_AddRef,
    dwritefont_Release,
    dwritefont_GetFontFamily,
    dwritefont_GetWeight,
    dwritefont_GetStretch,
    dwritefont_GetStyle,
    dwritefont_IsSymbolFont,
    dwritefont_GetFaceNames,
    dwritefont_GetInformationalStrings,
    dwritefont_GetSimulations,
    dwritefont_GetMetrics,
    dwritefont_HasCharacter,
    dwritefont_CreateFontFace
};

static HRESULT WINAPI dwritefontfamily_QueryInterface(IDWriteFontFamily *iface, REFIID riid, void **obj)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IDWriteFontList) ||
        IsEqualIID(riid, &IID_IDWriteFontFamily))
    {
        *obj = iface;
        IDWriteFontFamily_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontfamily_AddRef(IDWriteFontFamily *iface)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontfamily_Release(IDWriteFontFamily *iface)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        heap_free(This->familyname);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritefontfamily_GetFontCollection(IDWriteFontFamily *iface, IDWriteFontCollection **collection)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    FIXME("(%p)->(%p): stub\n", This, collection);
    return E_NOTIMPL;
}

static UINT32 WINAPI dwritefontfamily_GetFontCount(IDWriteFontFamily *iface)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    FIXME("(%p): stub\n", This);
    return 0;
}

static HRESULT WINAPI dwritefontfamily_GetFont(IDWriteFontFamily *iface, UINT32 index, IDWriteFont **font)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    FIXME("(%p)->(%u %p): stub\n", This, index, font);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontfamily_GetFamilyNames(IDWriteFontFamily *iface, IDWriteLocalizedStrings **names)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    static const WCHAR enusW[] = {'e','n','-','u','s',0};
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, names);

    hr = create_localizedstrings(names);
    if (FAILED(hr)) return hr;

    return add_localizedstring(*names, enusW, This->familyname);
}

static HRESULT WINAPI dwritefontfamily_GetFirstMatchingFont(IDWriteFontFamily *iface, DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFont **font)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    LOGFONTW lf;

    TRACE("(%p)->(%d %d %d %p)\n", This, weight, stretch, style, font);

    memset(&lf, 0, sizeof(lf));
    lf.lfWeight = weight;
    lf.lfItalic = style == DWRITE_FONT_STYLE_ITALIC;
    strcpyW(lf.lfFaceName, This->familyname);

    return create_font_from_logfont(&lf, font);
}

static HRESULT WINAPI dwritefontfamily_GetMatchingFonts(IDWriteFontFamily *iface, DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFontList **fonts)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily(iface);
    FIXME("(%p)->(%d %d %d %p): stub\n", This, weight, stretch, style, fonts);
    return E_NOTIMPL;
}

static const IDWriteFontFamilyVtbl fontfamilyvtbl = {
    dwritefontfamily_QueryInterface,
    dwritefontfamily_AddRef,
    dwritefontfamily_Release,
    dwritefontfamily_GetFontCollection,
    dwritefontfamily_GetFontCount,
    dwritefontfamily_GetFont,
    dwritefontfamily_GetFamilyNames,
    dwritefontfamily_GetFirstMatchingFont,
    dwritefontfamily_GetMatchingFonts
};

static HRESULT WINAPI dwritefontcollection_QueryInterface(IDWriteFontCollection *iface, REFIID riid, void **obj)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection(iface);
    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IDWriteFontCollection))
    {
        *obj = iface;
        IDWriteFontCollection_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontcollection_AddRef(IDWriteFontCollection *iface)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontcollection_Release(IDWriteFontCollection *iface)
{
    unsigned int i;
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        for (i = 0; i < This->count; i++)
            heap_free(This->families[i]);
        heap_free(This->families);
        heap_free(This);
    }

    return ref;
}

static UINT32 WINAPI dwritefontcollection_GetFontFamilyCount(IDWriteFontCollection *iface)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection(iface);
    TRACE("(%p)\n", This);
    return This->count;
}

static HRESULT WINAPI dwritefontcollection_GetFontFamily(IDWriteFontCollection *iface, UINT32 index, IDWriteFontFamily **family)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection(iface);

    TRACE("(%p)->(%u %p)\n", This, index, family);

    if (index >= This->count)
    {
        *family = NULL;
        return E_FAIL;
    }

    return create_fontfamily(This->families[index], family);
}

static HRESULT WINAPI dwritefontcollection_FindFamilyName(IDWriteFontCollection *iface, const WCHAR *name, UINT32 *index, BOOL *exists)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection(iface);
    UINT32 i;

    TRACE("(%p)->(%s %p %p)\n", This, debugstr_w(name), index, exists);

    for (i = 0; i < This->count; i++)
        if (!strcmpW(This->families[i], name))
        {
            *index = i;
            *exists = TRUE;
            return S_OK;
        }

    *index = (UINT32)-1;
    *exists = FALSE;

    return S_OK;
}

static HRESULT WINAPI dwritefontcollection_GetFontFromFontFace(IDWriteFontCollection *iface, IDWriteFontFace *face, IDWriteFont **font)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection(iface);
    FIXME("(%p)->(%p %p): stub\n", This, face, font);
    return E_NOTIMPL;
}

static const IDWriteFontCollectionVtbl fontcollectionvtbl = {
    dwritefontcollection_QueryInterface,
    dwritefontcollection_AddRef,
    dwritefontcollection_Release,
    dwritefontcollection_GetFontFamilyCount,
    dwritefontcollection_GetFontFamily,
    dwritefontcollection_FindFamilyName,
    dwritefontcollection_GetFontFromFontFace
};

static HRESULT add_family_syscollection(struct dwrite_fontcollection *collection, const WCHAR *family)
{
    /* check for duplicate family name */
    if (collection->count && !strcmpW(collection->families[collection->count-1], family)) return S_OK;

    /* double array length */
    if (collection->count == collection->alloc)
    {
        collection->alloc *= 2;
        collection->families = heap_realloc(collection->families, collection->alloc*sizeof(WCHAR*));
    }

    collection->families[collection->count++] = heap_strdupW(family);
    TRACE("family name %s\n", debugstr_w(family));

    return S_OK;
}

static INT CALLBACK enum_font_families(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lParam)
{
    struct dwrite_fontcollection *collection = (struct dwrite_fontcollection*)lParam;
    return add_family_syscollection(collection, lf->lfFaceName) == S_OK;
}

HRESULT get_system_fontcollection(IDWriteFontCollection **collection)
{
    struct dwrite_fontcollection *This;
    LOGFONTW lf;
    HDC hdc;

    *collection = NULL;

    This = heap_alloc(sizeof(struct dwrite_fontcollection));
    if (!This) return E_OUTOFMEMORY;

    This->IDWriteFontCollection_iface.lpVtbl = &fontcollectionvtbl;
    This->ref = 1;
    This->alloc = 50;
    This->count = 0;
    This->families = heap_alloc(This->alloc*sizeof(WCHAR*));

    TRACE("building system font collection:\n");

    hdc = CreateCompatibleDC(0);
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfPitchAndFamily = DEFAULT_PITCH;
    lf.lfFaceName[0] = 0;
    EnumFontFamiliesExW(hdc, &lf, enum_font_families, (LPARAM)This, 0);
    DeleteDC(hdc);

    *collection = &This->IDWriteFontCollection_iface;

    return S_OK;
}

static HRESULT create_fontfamily(const WCHAR *familyname, IDWriteFontFamily **family)
{
    struct dwrite_fontfamily *This;

    *family = NULL;

    This = heap_alloc(sizeof(struct dwrite_fontfamily));
    if (!This) return E_OUTOFMEMORY;

    This->IDWriteFontFamily_iface.lpVtbl = &fontfamilyvtbl;
    This->ref = 1;
    This->familyname = heap_strdupW(familyname);

    *family = &This->IDWriteFontFamily_iface;

    return S_OK;
}

static void get_font_properties(struct dwrite_font *font, HDC hdc)
{
    TT_OS2_V2 tt_os2;
    TT_HEAD tt_head;
    TT_POST tt_post;
    LONG size;

    /* default stretch and weight to normal */
    font->data->stretch = DWRITE_FONT_STRETCH_NORMAL;
    font->data->weight = DWRITE_FONT_WEIGHT_NORMAL;

    memset(&font->data->metrics, 0, sizeof(font->data->metrics));

    size = GetFontData(hdc, MS_OS2_TAG, 0, NULL, 0);
    if (size != GDI_ERROR)
    {
        if (size > sizeof(tt_os2)) size = sizeof(tt_os2);

        memset(&tt_os2, 0, sizeof(tt_os2));
        if (GetFontData(hdc, MS_OS2_TAG, 0, &tt_os2, size) != size) return;

        /* DWRITE_FONT_STRETCH enumeration values directly match font data values */
        if (GET_BE_WORD(tt_os2.usWidthClass) <= DWRITE_FONT_STRETCH_ULTRA_EXPANDED)
            font->data->stretch = GET_BE_WORD(tt_os2.usWidthClass);

        font->data->weight = GET_BE_WORD(tt_os2.usWeightClass);
        TRACE("stretch=%d, weight=%d\n", font->data->stretch, font->data->weight);

        font->data->metrics.ascent    = GET_BE_WORD(tt_os2.sTypoAscender);
        font->data->metrics.descent   = GET_BE_WORD(tt_os2.sTypoDescender);
        font->data->metrics.lineGap   = GET_BE_WORD(tt_os2.sTypoLineGap);
        font->data->metrics.capHeight = GET_BE_WORD(tt_os2.sCapHeight);
        font->data->metrics.xHeight   = GET_BE_WORD(tt_os2.sxHeight);
        font->data->metrics.strikethroughPosition  = GET_BE_WORD(tt_os2.yStrikeoutPosition);
        font->data->metrics.strikethroughThickness = GET_BE_WORD(tt_os2.yStrikeoutSize);
    }

    memset(&tt_head, 0, sizeof(tt_head));
    if (GetFontData(hdc, MS_HEAD_TAG, 0, &tt_head, sizeof(tt_head)) != GDI_ERROR)
    {
        font->data->metrics.designUnitsPerEm = GET_BE_WORD(tt_head.unitsPerEm);
    }

    memset(&tt_post, 0, sizeof(tt_post));
    if (GetFontData(hdc, MS_POST_TAG, 0, &tt_post, sizeof(tt_post)) != GDI_ERROR)
    {
        font->data->metrics.underlinePosition = GET_BE_WORD(tt_post.underlinePosition);
        font->data->metrics.underlineThickness = GET_BE_WORD(tt_post.underlineThickness);
    }
}

static HRESULT create_font_base(IDWriteFont **font)
{
    struct dwrite_font *This;
    *font = NULL;

    This = heap_alloc(sizeof(struct dwrite_font));
    if (!This) return E_OUTOFMEMORY;
    This->data = heap_alloc(sizeof(struct dwrite_font_data));
    if (!This->data)
    {
        heap_free(This);
        return E_OUTOFMEMORY;
    }

    This->IDWriteFont_iface.lpVtbl = &dwritefontvtbl;
    This->ref = 1;
    This->data->face_data = NULL;
    This->face = NULL;
    This->family = NULL;
    This->is_system = FALSE;

    *font = &This->IDWriteFont_iface;

    return S_OK;
}

HRESULT create_font_from_logfont(const LOGFONTW *logfont, IDWriteFont **font)
{
    const WCHAR* facename, *familyname;
    struct dwrite_font *This;
    IDWriteFontFamily *family;
    OUTLINETEXTMETRICW *otm;
    HRESULT hr;
    HFONT hfont;
    HDC hdc;
    int ret;

    hr = create_font_base(font);
    if (FAILED(hr))
        return hr;

    This = impl_from_IDWriteFont(*font);

    hfont = CreateFontIndirectW(logfont);
    if (!hfont)
    {
        heap_free(This->data);
        heap_free(This);
        return DWRITE_E_NOFONT;
    }

    hdc = CreateCompatibleDC(0);
    SelectObject(hdc, hfont);

    ret = GetOutlineTextMetricsW(hdc, 0, NULL);
    otm = heap_alloc(ret);
    if (!otm)
    {
        heap_free(This->data);
        heap_free(This);
        DeleteDC(hdc);
        DeleteObject(hfont);
        return E_OUTOFMEMORY;
    }
    otm->otmSize = ret;
    ret = GetOutlineTextMetricsW(hdc, otm->otmSize, otm);

    get_font_properties(This, hdc);

    DeleteDC(hdc);
    DeleteObject(hfont);

    facename = (WCHAR*)((char*)otm + (ptrdiff_t)otm->otmpFaceName);
    familyname = (WCHAR*)((char*)otm + (ptrdiff_t)otm->otmpFamilyName);
    TRACE("facename=%s, familyname=%s\n", debugstr_w(facename), debugstr_w(familyname));

    hr = create_fontfamily(familyname, &family);
    heap_free(otm);
    if (hr != S_OK)
    {
        heap_free(This->data);
        heap_free(This);
        return hr;
    }

    This->is_system = TRUE;
    This->family = family;
    This->data->simulations = DWRITE_FONT_SIMULATIONS_NONE;
    This->data->style = logfont->lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    This->data->facename = heap_strdupW(logfont->lfFaceName);

    return S_OK;
}

static HRESULT WINAPI dwritefontfile_QueryInterface(IDWriteFontFile *iface, REFIID riid, void **obj)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFile))
    {
        *obj = iface;
        IDWriteFontFile_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontfile_AddRef(IDWriteFontFile *iface)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontfile_Release(IDWriteFontFile *iface)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        IDWriteFontFileLoader_Release(This->loader);
        if (This->stream) IDWriteFontFileStream_Release(This->stream);
        heap_free(This->reference_key);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritefontfile_GetReferenceKey(IDWriteFontFile *iface, const void **fontFileReferenceKey, UINT32 *fontFileReferenceKeySize)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    TRACE("(%p)->(%p, %p)\n", This, fontFileReferenceKey, fontFileReferenceKeySize);
    *fontFileReferenceKey = This->reference_key;
    *fontFileReferenceKeySize = This->key_size;

    return S_OK;
}

static HRESULT WINAPI dwritefontfile_GetLoader(IDWriteFontFile *iface, IDWriteFontFileLoader **fontFileLoader)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    TRACE("(%p)->(%p)\n", This, fontFileLoader);
    *fontFileLoader = This->loader;
    IDWriteFontFileLoader_AddRef(This->loader);

    return S_OK;
}

static HRESULT WINAPI dwritefontfile_Analyze(IDWriteFontFile *iface, BOOL *isSupportedFontType, DWRITE_FONT_FILE_TYPE *fontFileType, DWRITE_FONT_FACE_TYPE *fontFaceType, UINT32 *numberOfFaces)
{
    HRESULT hr;
    const void *font_data;
    void *context;
    IDWriteFontFileStream *stream;

    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    FIXME("(%p)->(%p, %p, %p, %p): Stub\n", This, isSupportedFontType, fontFileType, fontFaceType, numberOfFaces);

    *isSupportedFontType = FALSE;
    *fontFileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    if (fontFaceType)
        *fontFaceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    *numberOfFaces = 0;

    hr = IDWriteFontFileLoader_CreateStreamFromKey(This->loader, This->reference_key, This->key_size, &stream);
    if (FAILED(hr))
        return S_OK;
    hr = IDWriteFontFileStream_ReadFileFragment(stream, &font_data, 0, 28, &context);
    if (SUCCEEDED(hr))
    {
        hr = analyze_opentype_font(font_data, numberOfFaces, fontFileType, fontFaceType, isSupportedFontType);
        IDWriteFontFileStream_ReleaseFileFragment(stream, context);
    }
    /* TODO: Further Analysis */
    IDWriteFontFileStream_Release(stream);
    return S_OK;
}

static const IDWriteFontFileVtbl dwritefontfilevtbl = {
    dwritefontfile_QueryInterface,
    dwritefontfile_AddRef,
    dwritefontfile_Release,
    dwritefontfile_GetReferenceKey,
    dwritefontfile_GetLoader,
    dwritefontfile_Analyze,
};

HRESULT create_font_file(IDWriteFontFileLoader *loader, const void *reference_key, UINT32 key_size, IDWriteFontFile **font_file)
{
    struct dwrite_fontfile *This;

    This = heap_alloc(sizeof(struct dwrite_fontfile));
    if (!This) return E_OUTOFMEMORY;

    This->IDWriteFontFile_iface.lpVtbl = &dwritefontfilevtbl;
    This->ref = 1;
    IDWriteFontFileLoader_AddRef(loader);
    This->loader = loader;
    This->stream = NULL;
    This->reference_key = heap_alloc(key_size);
    memcpy(This->reference_key, reference_key, key_size);
    This->key_size = key_size;

    *font_file = &This->IDWriteFontFile_iface;

    return S_OK;
}

HRESULT font_create_fontface(IDWriteFactory *iface, DWRITE_FONT_FACE_TYPE facetype, UINT32 files_number, IDWriteFontFile* const* font_files, UINT32 index, DWRITE_FONT_SIMULATIONS sim_flags, IDWriteFontFace **font_face)
{
    int i;
    struct dwrite_fontface *This;
    HRESULT hr = S_OK;

    *font_face = NULL;

    This = heap_alloc(sizeof(struct dwrite_fontface));
    if (!This) return E_OUTOFMEMORY;
    This->data = heap_alloc(sizeof(struct dwrite_fontface_data));
    if (!This)
    {
        heap_free(This);
        return E_OUTOFMEMORY;
    }

    This->IDWriteFontFace_iface.lpVtbl = &dwritefontfacevtbl;
    This->ref = 1;
    This->data->ref = 1;
    This->data->type = facetype;
    This->data->file_count = files_number;
    This->data->files = heap_alloc(sizeof(*This->data->files) * files_number);
    This->CMAP_table = NULL;
    This->CMAP_context = NULL;
    This->CMAP_size = 0;
    /* Verify font file streams */
    for (i = 0; i < This->data->file_count && SUCCEEDED(hr); i++)
    {
        IDWriteFontFileStream *stream;
        hr = _dwritefontfile_GetFontFileStream(font_files[i], &stream);
        if (SUCCEEDED(hr))
            IDWriteFontFileStream_Release(stream);
    }
    if (FAILED(hr))
    {
        heap_free(This->data->files);
        heap_free(This->data);
        heap_free(This);
        return hr;
    }
    for (i = 0; i < This->data->file_count; i++)
    {
        This->data->files[i] = font_files[i];
        IDWriteFontFile_AddRef(font_files[i]);
    }

    This->data->index = index;
    This->data->simulations = sim_flags;
    This->is_system = FALSE;

    *font_face = &This->IDWriteFontFace_iface;

    return S_OK;
}

/* IDWriteLocalFontFileLoader and its required IDWriteFontFileStream */

struct dwrite_localfontfilestream
{
    IDWriteFontFileStream IDWriteFontFileStream_iface;
    LONG ref;

    HANDLE handle;
};

struct dwrite_localfontfileloader {
    IDWriteLocalFontFileLoader IDWriteLocalFontFileLoader_iface;
    LONG ref;
};

static inline struct dwrite_localfontfileloader *impl_from_IDWriteLocalFontFileLoader(IDWriteLocalFontFileLoader *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_localfontfileloader, IDWriteLocalFontFileLoader_iface);
}

static inline struct dwrite_localfontfilestream *impl_from_IDWriteFontFileStream(IDWriteFontFileStream *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_localfontfilestream, IDWriteFontFileStream_iface);
}

static HRESULT WINAPI localfontfilestream_QueryInterface(IDWriteFontFileStream *iface, REFIID riid, void **obj)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileStream))
    {
        *obj = iface;
        IDWriteFontFileStream_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI localfontfilestream_AddRef(IDWriteFontFileStream *iface)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI localfontfilestream_Release(IDWriteFontFileStream *iface)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        if (This->handle != INVALID_HANDLE_VALUE)
            CloseHandle(This->handle);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI localfontfilestream_ReadFileFragment(IDWriteFontFileStream *iface, void const **fragment_start, UINT64 offset, UINT64 fragment_size, void **fragment_context)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    LARGE_INTEGER distance;
    DWORD bytes = fragment_size;
    DWORD read;

    TRACE("(%p)->(%p, %s, %s, %p)\n",This, fragment_start,
          wine_dbgstr_longlong(offset), wine_dbgstr_longlong(fragment_size), fragment_context);

    *fragment_context = NULL;
    distance.QuadPart = offset;
    if (!SetFilePointerEx(This->handle, distance, NULL, FILE_BEGIN))
        return E_FAIL;
    *fragment_start = *fragment_context = heap_alloc(bytes);
    if (!*fragment_context)
        return E_FAIL;
    if (!ReadFile(This->handle, *fragment_context, bytes, &read, NULL))
    {
        heap_free(*fragment_context);
        return E_FAIL;
    }

    return S_OK;
}

static void WINAPI localfontfilestream_ReleaseFileFragment(IDWriteFontFileStream *iface, void *fragment_context)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    TRACE("(%p)->(%p)\n", This, fragment_context);
    heap_free(fragment_context);
}

static HRESULT WINAPI localfontfilestream_GetFileSize(IDWriteFontFileStream *iface, UINT64 *size)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    LARGE_INTEGER li;
    TRACE("(%p)->(%p)\n",This, size);
    GetFileSizeEx(This->handle, &li);
    *size = li.QuadPart;
    return S_OK;
}

static HRESULT WINAPI localfontfilestream_GetLastWriteTime(IDWriteFontFileStream *iface, UINT64 *last_writetime)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    FIXME("(%p)->(%p): stub\n",This, last_writetime);
    *last_writetime = 0;
    return E_NOTIMPL;
}

static const IDWriteFontFileStreamVtbl localfontfilestreamvtbl =
{
    localfontfilestream_QueryInterface,
    localfontfilestream_AddRef,
    localfontfilestream_Release,
    localfontfilestream_ReadFileFragment,
    localfontfilestream_ReleaseFileFragment,
    localfontfilestream_GetFileSize,
    localfontfilestream_GetLastWriteTime
};

static HRESULT create_localfontfilestream(HANDLE handle, IDWriteFontFileStream** iface)
{
    struct dwrite_localfontfilestream *This = heap_alloc(sizeof(struct dwrite_localfontfilestream));
    if (!This)
        return E_OUTOFMEMORY;

    This->ref = 1;
    This->handle = handle;
    This->IDWriteFontFileStream_iface.lpVtbl = &localfontfilestreamvtbl;

    *iface = &This->IDWriteFontFileStream_iface;
    return S_OK;
}

static HRESULT WINAPI localfontfileloader_QueryInterface(IDWriteLocalFontFileLoader *iface, REFIID riid, void **obj)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileLoader) || IsEqualIID(riid, &IID_IDWriteLocalFontFileLoader))
    {
        *obj = iface;
        IDWriteLocalFontFileLoader_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI localfontfileloader_AddRef(IDWriteLocalFontFileLoader *iface)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI localfontfileloader_Release(IDWriteLocalFontFileLoader *iface)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
        heap_free(This);

    return ref;
}

static HRESULT WINAPI localfontfileloader_CreateStreamFromKey(IDWriteLocalFontFileLoader *iface, const void *fontFileReferenceKey, UINT32 fontFileReferenceKeySize, IDWriteFontFileStream **fontFileStream)
{
    HANDLE handle;
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    const WCHAR *name = (const WCHAR*)fontFileReferenceKey;

    TRACE("(%p)->(%p, %i, %p)\n",This, fontFileReferenceKey, fontFileReferenceKeySize, fontFileStream);

    TRACE("name: %s\n",debugstr_w(name));
    handle = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (handle == INVALID_HANDLE_VALUE)
        return E_FAIL;

    return create_localfontfilestream(handle, fontFileStream);
}

static HRESULT WINAPI localfontfileloader_GetFilePathLengthFromKey(IDWriteLocalFontFileLoader *iface, void const *key, UINT32 key_size, UINT32 *length)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    TRACE("(%p)->(%p, %i, %p)\n",This, key, key_size, length);
    *length = key_size;
    return S_OK;
}

static HRESULT WINAPI localfontfileloader_GetFilePathFromKey(IDWriteLocalFontFileLoader *iface, void const *key, UINT32 key_size, WCHAR *path, UINT32 length)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    TRACE("(%p)->(%p, %i, %p, %i)\n",This, key, key_size, path, length);
    if (length < key_size)
        return E_INVALIDARG;
    lstrcpynW((WCHAR*)key, path, key_size);
    return S_OK;
}

static HRESULT WINAPI localfontfileloader_GetLastWriteTimeFromKey(IDWriteLocalFontFileLoader *iface, void const *key, UINT32 key_size, FILETIME *writetime)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    FIXME("(%p)->(%p, %i, %p):stub\n",This, key, key_size, writetime);
    return E_NOTIMPL;
}

static const struct IDWriteLocalFontFileLoaderVtbl localfontfileloadervtbl = {
    localfontfileloader_QueryInterface,
    localfontfileloader_AddRef,
    localfontfileloader_Release,
    localfontfileloader_CreateStreamFromKey,
    localfontfileloader_GetFilePathLengthFromKey,
    localfontfileloader_GetFilePathFromKey,
    localfontfileloader_GetLastWriteTimeFromKey
};

HRESULT create_localfontfileloader(IDWriteLocalFontFileLoader** iface)
{
    struct dwrite_localfontfileloader *This = heap_alloc(sizeof(struct dwrite_localfontfileloader));
    if (!This)
        return E_OUTOFMEMORY;

    This->ref = 1;
    This->IDWriteLocalFontFileLoader_iface.lpVtbl = &localfontfileloadervtbl;

    *iface = &This->IDWriteLocalFontFileLoader_iface;
    return S_OK;
}
