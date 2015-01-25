/*
 *    Text analyzing tests
 *
 * Copyright 2012-2014 Nikolay Sivov for CodeWeavers
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

#include <assert.h>

#include "initguid.h"
#include "windows.h"
#include "dwrite.h"
#include "dwrite_2.h"

#include "wine/test.h"

static IDWriteFactory *factory;
static const WCHAR test_fontfile[] = {'w','i','n','e','_','t','e','s','t','_','f','o','n','t','.','t','t','f',0};

enum analysis_kind {
    ScriptAnalysis,
    LastKind
};

static const char *get_analysis_kind_name(enum analysis_kind kind)
{
    switch (kind)
    {
    case ScriptAnalysis:
        return "ScriptAnalysis";
    default:
        return "unknown";
    }
}

struct script_analysis {
    UINT32 pos;
    UINT32 len;
    DWRITE_SCRIPT_SHAPES shapes;
};

struct call_entry {
    enum analysis_kind kind;
    struct script_analysis sa;
};

struct testcontext {
    enum analysis_kind kind;
    BOOL todo;
    int *failcount;
    const char *file;
    int line;
};

struct call_sequence
{
    int count;
    int size;
    struct call_entry *sequence;
};

#define NUM_CALL_SEQUENCES    1
#define ANALYZER_ID 0
static struct call_sequence *sequences[NUM_CALL_SEQUENCES];
static struct call_sequence *expected_seq[1];

static void add_call(struct call_sequence **seq, int sequence_index, const struct call_entry *call)
{
    struct call_sequence *call_seq = seq[sequence_index];

    if (!call_seq->sequence)
    {
        call_seq->size = 10;
        call_seq->sequence = HeapAlloc(GetProcessHeap(), 0,
                                      call_seq->size * sizeof (struct call_entry));
    }

    if (call_seq->count == call_seq->size)
    {
        call_seq->size *= 2;
        call_seq->sequence = HeapReAlloc(GetProcessHeap(), 0,
                                        call_seq->sequence,
                                        call_seq->size * sizeof (struct call_entry));
    }

    assert(call_seq->sequence);

    call_seq->sequence[call_seq->count++] = *call;
}

static inline void flush_sequence(struct call_sequence **seg, int sequence_index)
{
    struct call_sequence *call_seq = seg[sequence_index];

    HeapFree(GetProcessHeap(), 0, call_seq->sequence);
    call_seq->sequence = NULL;
    call_seq->count = call_seq->size = 0;
}

static inline void flush_sequences(struct call_sequence **seq, int n)
{
    int i;
    for (i = 0; i < n; i++)
        flush_sequence(seq, i);
}

static void init_call_sequences(struct call_sequence **seq, int n)
{
    int i;

    for (i = 0; i < n; i++)
        seq[i] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct call_sequence));
}

static void test_uint(UINT32 actual, UINT32 expected, const char *name, const struct testcontext *ctxt)
{
    if (expected != actual && ctxt->todo)
    {
        (*ctxt->failcount)++;
        ok_(ctxt->file, ctxt->line) (0, "%s: \"%s\" expecting %u, got %u\n", get_analysis_kind_name(ctxt->kind), name, expected, actual);
    }
    else
        ok_(ctxt->file, ctxt->line) (expected == actual, "%s: \"%s\" expecting %u, got %u\n", get_analysis_kind_name(ctxt->kind), name,
            expected, actual);
}

static void ok_sequence_(struct call_sequence **seq, int sequence_index,
    const struct call_entry *expected, const char *context, BOOL todo,
    const char *file, int line)
{
    struct call_sequence *call_seq = seq[sequence_index];
    static const struct call_entry end_of_sequence = { LastKind };
    const struct call_entry *actual, *sequence;
    int failcount = 0;
    struct testcontext ctxt;

    add_call(seq, sequence_index, &end_of_sequence);

    sequence = call_seq->sequence;
    actual = sequence;

    ctxt.failcount = &failcount;
    ctxt.todo = todo;
    ctxt.file = file;
    ctxt.line = line;

    while (expected->kind != LastKind && actual->kind != LastKind)
    {
        if (expected->kind == actual->kind)
        {
            ctxt.kind = expected->kind;

            switch (actual->kind)
            {
            case ScriptAnalysis:
                test_uint(actual->sa.pos, expected->sa.pos, "position", &ctxt);
                test_uint(actual->sa.len, expected->sa.len, "length", &ctxt);
                test_uint(actual->sa.shapes, expected->sa.shapes, "shapes", &ctxt);
                break;
            default:
                ok(0, "%s: callback not handled, %s\n", context, get_analysis_kind_name(actual->kind));
            }
            expected++;
            actual++;
        }
        else if (todo)
        {
            failcount++;
            todo_wine
            {
                ok_(file, line) (0, "%s: call %s was expected, but got call %s instead\n",
                    context, get_analysis_kind_name(expected->kind), get_analysis_kind_name(actual->kind));
            }

            flush_sequence(seq, sequence_index);
            return;
        }
        else
        {
            ok_(file, line) (0, "%s: call %s was expected, but got call %s instead\n",
                context, get_analysis_kind_name(expected->kind), get_analysis_kind_name(actual->kind));
            expected++;
            actual++;
        }
    }

    if (todo)
    {
        todo_wine
        {
            if (expected->kind != LastKind || actual->kind != LastKind)
            {
                failcount++;
                ok_(file, line) (0, "%s: the call sequence is not complete: expected %s - actual %s\n",
                    context, get_analysis_kind_name(expected->kind), get_analysis_kind_name(actual->kind));
            }
        }
    }
    else if (expected->kind != LastKind || actual->kind != LastKind)
    {
        ok_(file, line) (0, "%s: the call sequence is not complete: expected %s - actual %s\n",
            context, get_analysis_kind_name(expected->kind), get_analysis_kind_name(actual->kind));
    }

    if (todo && !failcount) /* succeeded yet marked todo */
    {
        todo_wine
        {
            ok_(file, line)(1, "%s: marked \"todo_wine\" but succeeds\n", context);
        }
    }

    flush_sequence(seq, sequence_index);
}

#define ok_sequence(seq, index, exp, contx, todo) \
        ok_sequence_(seq, index, (exp), (contx), (todo), __FILE__, __LINE__)

static HRESULT WINAPI analysissink_QueryInterface(IDWriteTextAnalysisSink *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IDWriteTextAnalysisSink) || IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI analysissink_AddRef(IDWriteTextAnalysisSink *iface)
{
    return 2;
}

static ULONG WINAPI analysissink_Release(IDWriteTextAnalysisSink *iface)
{
    return 1;
}

static HRESULT WINAPI analysissink_SetScriptAnalysis(IDWriteTextAnalysisSink *iface,
    UINT32 position, UINT32 length, DWRITE_SCRIPT_ANALYSIS const* sa)
{
    struct call_entry entry;

    entry.kind = ScriptAnalysis;
    entry.sa.pos = position;
    entry.sa.len = length;
    entry.sa.shapes = sa->shapes;
    add_call(sequences, ANALYZER_ID, &entry);
    return S_OK;
}

static DWRITE_SCRIPT_ANALYSIS g_sa;
static HRESULT WINAPI analysissink_SetScriptAnalysis2(IDWriteTextAnalysisSink *iface,
    UINT32 position, UINT32 length, DWRITE_SCRIPT_ANALYSIS const* sa)
{
    g_sa = *sa;
    return S_OK;
}

#define BREAKPOINT_COUNT 20
static DWRITE_LINE_BREAKPOINT g_actual_bp[BREAKPOINT_COUNT];

static HRESULT WINAPI analysissink_SetLineBreakpoints(IDWriteTextAnalysisSink *iface,
        UINT32 position,
        UINT32 length,
        DWRITE_LINE_BREAKPOINT const* breakpoints)
{
    if (position + length > BREAKPOINT_COUNT) {
        ok(0, "SetLineBreakpoints: reported pos=%u, len=%u overflows expected length %d\n", position, length, BREAKPOINT_COUNT);
        return E_FAIL;
    }
    memcpy(&g_actual_bp[position], breakpoints, length*sizeof(DWRITE_LINE_BREAKPOINT));
    return S_OK;
}

static HRESULT WINAPI analysissink_SetBidiLevel(IDWriteTextAnalysisSink *iface,
        UINT32 position,
        UINT32 length,
        UINT8 explicitLevel,
        UINT8 resolvedLevel)
{
    ok(0, "unexpected\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI analysissink_SetNumberSubstitution(IDWriteTextAnalysisSink *iface,
        UINT32 position,
        UINT32 length,
        IDWriteNumberSubstitution* substitution)
{
    ok(0, "unexpected\n");
    return E_NOTIMPL;
}

static IDWriteTextAnalysisSinkVtbl analysissinkvtbl = {
    analysissink_QueryInterface,
    analysissink_AddRef,
    analysissink_Release,
    analysissink_SetScriptAnalysis,
    analysissink_SetLineBreakpoints,
    analysissink_SetBidiLevel,
    analysissink_SetNumberSubstitution
};

static IDWriteTextAnalysisSinkVtbl analysissinkvtbl2 = {
    analysissink_QueryInterface,
    analysissink_AddRef,
    analysissink_Release,
    analysissink_SetScriptAnalysis2,
    analysissink_SetLineBreakpoints,
    analysissink_SetBidiLevel,
    analysissink_SetNumberSubstitution
};

static IDWriteTextAnalysisSink analysissink = { &analysissinkvtbl };
static IDWriteTextAnalysisSink analysissink2 = { &analysissinkvtbl2 };

static HRESULT WINAPI analysissource_QueryInterface(IDWriteTextAnalysisSource *iface,
    REFIID riid, void **obj)
{
    ok(0, "QueryInterface not expected\n");
    return E_NOTIMPL;
}

static ULONG WINAPI analysissource_AddRef(IDWriteTextAnalysisSource *iface)
{
    ok(0, "AddRef not expected\n");
    return 2;
}

static ULONG WINAPI analysissource_Release(IDWriteTextAnalysisSource *iface)
{
    ok(0, "Release not expected\n");
    return 1;
}

static const WCHAR *g_source;

static HRESULT WINAPI analysissource_GetTextAtPosition(IDWriteTextAnalysisSource *iface,
    UINT32 position, WCHAR const** text, UINT32* text_len)
{
    if (position >= lstrlenW(g_source))
    {
        *text = NULL;
        *text_len = 0;
    }
    else
    {
        *text = &g_source[position];
        *text_len = lstrlenW(g_source) - position;
    }

    return S_OK;
}

static HRESULT WINAPI analysissource_GetTextBeforePosition(IDWriteTextAnalysisSource *iface,
    UINT32 position, WCHAR const** text, UINT32* text_len)
{
    ok(0, "unexpected\n");
    return E_NOTIMPL;
}

static DWRITE_READING_DIRECTION WINAPI analysissource_GetParagraphReadingDirection(
    IDWriteTextAnalysisSource *iface)
{
    ok(0, "unexpected\n");
    return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
}

static HRESULT WINAPI analysissource_GetLocaleName(IDWriteTextAnalysisSource *iface,
    UINT32 position, UINT32* text_len, WCHAR const** locale)
{
    *locale = NULL;
    *text_len = 0;
    return S_OK;
}

static HRESULT WINAPI analysissource_GetNumberSubstitution(IDWriteTextAnalysisSource *iface,
    UINT32 position, UINT32* text_len, IDWriteNumberSubstitution **substitution)
{
    ok(0, "unexpected\n");
    return E_NOTIMPL;
}

static IDWriteTextAnalysisSourceVtbl analysissourcevtbl = {
    analysissource_QueryInterface,
    analysissource_AddRef,
    analysissource_Release,
    analysissource_GetTextAtPosition,
    analysissource_GetTextBeforePosition,
    analysissource_GetParagraphReadingDirection,
    analysissource_GetLocaleName,
    analysissource_GetNumberSubstitution
};

static IDWriteTextAnalysisSource analysissource = { &analysissourcevtbl };

static IDWriteFontFace *create_fontface(void)
{
    static const WCHAR tahomaW[] = {'T','a','h','o','m','a',0};
    IDWriteGdiInterop *interop;
    IDWriteFontFace *fontface;
    IDWriteFont *font;
    LOGFONTW logfont;
    HRESULT hr;

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFont_Release(font);
    IDWriteGdiInterop_Release(interop);

    return fontface;
}

static void create_testfontfile(const WCHAR *filename)
{
    DWORD written;
    HANDLE file;
    HRSRC res;
    void *ptr;

    file = CreateFileW(filename, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "file creation failed\n");

    res = FindResourceA(GetModuleHandleA(NULL), (LPCSTR)MAKEINTRESOURCE(1), (LPCSTR)RT_RCDATA);
    ok(res != 0, "couldn't find resource\n");
    ptr = LockResource(LoadResource(GetModuleHandleA(NULL), res));
    WriteFile(file, ptr, SizeofResource(GetModuleHandleA(NULL), res), &written, NULL);
    ok(written == SizeofResource(GetModuleHandleA(NULL), res), "couldn't write resource\n");
    CloseHandle(file);
}

static IDWriteFontFace *create_testfontface(const WCHAR *filename)
{
    IDWriteFontFace *face;
    IDWriteFontFile *file;
    HRESULT hr;

    hr = IDWriteFactory_CreateFontFileReference(factory, filename, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0,
        DWRITE_FONT_SIMULATIONS_NONE, &face);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    return face;
}

struct sa_test {
    const WCHAR string[50];
    int item_count;
    struct script_analysis sa[10];
};

static struct sa_test sa_tests[] = {
    {
      /* just 1 char string */
      {'t',0}, 1,
          { { 0, 1, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      {'t','e','s','t',0}, 1,
          { { 0, 4, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      {' ',' ',' ',' ','!','$','[','^','{','~',0}, 1,
          { { 0, 10, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      {' ',' ',' ','1','2',' ',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* digits only */
      {'1','2',0}, 1,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Arabic */
      {0x064a,0x064f,0x0633,0x0627,0x0648,0x0650,0x064a,0}, 1,
          { { 0, 7, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Arabic */
      {0x0627,0x0644,0x0635,0x0651,0x0650,0x062d,0x0629,0x064f,' ',0x062a,0x064e,
       0x0627,0x062c,0x064c,' ',0x0639,0x064e,0x0644,0x0649,' ',
       0x0631,0x064f,0x0624,0x0648,0x0633,0x0650,' ',0x0627,0x0644,
       0x0623,0x0635,0x0650,0x062d,0x0651,0x064e,0x0627,0x0621,0x0650,0x06f0,0x06f5,0}, 1,
          { { 0, 40, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Arabic, Latin */
      {'1','2','3','-','5','2',0x064a,0x064f,0x0633,0x0627,0x0648,0x0650,0x064a,'7','1','.',0}, 1,
          { { 0, 16, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Arabic, English */
      {'A','B','C','-','D','E','F',' ',0x0621,0x0623,0x0624,0}, 2,
          { { 0, 8, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 8, 3, DWRITE_SCRIPT_SHAPES_DEFAULT },
          }
    },
    {
      /* leading space, Arabic, English */
      {' ',0x0621,0x0623,0x0624,'A','B','C','-','D','E','F',0}, 2,
          { { 0, 4, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 4, 7, DWRITE_SCRIPT_SHAPES_DEFAULT },
          }
    },
    {
      /* English, Arabic, trailing space */
      {'A','B','C','-','D','E','F',0x0621,0x0623,0x0624,' ',0}, 2,
          { { 0, 7, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 7, 4, DWRITE_SCRIPT_SHAPES_DEFAULT },
          }
    },
    {
      /* C1 Controls, Latin-1 Supplement */
      {0x80,0x90,0x9f,0xa0,0xc0,0xb8,0xbf,0xc0,0xff,0}, 2,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_NO_VISUAL },
            { 3, 6, DWRITE_SCRIPT_SHAPES_DEFAULT },
          }
    },
    {
      /* Latin Extended-A */
      {0x100,0x120,0x130,0x140,0x150,0x160,0x170,0x17f,0}, 1,
          { { 0, 8, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Latin Extended-B */
      {0x180,0x190,0x1bf,0x1c0,0x1c3,0x1c4,0x1cc,0x1dc,0x1ff,0x217,0x21b,0x24f,0}, 1,
          { { 0, 12, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* IPA Extensions */
      {0x250,0x260,0x270,0x290,0x2af,0}, 1,
          { { 0, 5, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Spacing Modifier Letters */
      {0x2b0,0x2ba,0x2d7,0x2dd,0x2ef,0x2ff,0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Combining Diacritical Marks */
      {0x300,0x320,0x340,0x345,0x350,0x36f,0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Greek and Coptic */
      {0x370,0x388,0x3d8,0x3e1,0x3e2,0x3fa,0x3ff,0}, 3,
          { { 0, 4, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 4, 1, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 5, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }
          }
    },
    {
      /* Cyrillic and Cyrillic Supplement */
      {0x400,0x40f,0x410,0x44f,0x450,0x45f,0x460,0x481,0x48a,0x4f0,0x4fa,0x4ff,0x500,0x510,0x520,0}, 1,
          { { 0, 15, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Armenian */
      {0x531,0x540,0x559,0x55f,0x570,0x589,0x58a,0}, 1,
          { { 0, 7, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Hebrew */
      {0x5e9,0x5dc,0x5d5,0x5dd,0}, 1,
          { { 0, 4, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Latin, Hebrew, Latin */
      {'p','a','r','t',' ','o','n','e',' ',0x5d7,0x5dc,0x5e7,' ',0x5e9,0x5ea,0x5d9,0x5d9,0x5dd,' ','p','a','r','t',' ','t','h','r','e','e',0}, 3,
          { { 0, 9, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 9, 10, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 19, 10, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Syriac */
      {0x710,0x712,0x712,0x714,'.',0}, 1,
          { { 0, 5, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Arabic Supplement */
      {0x750,0x760,0x76d,'.',0}, 1,
          { { 0, 4, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Thaana */
      {0x780,0x78e,0x798,0x7a6,0x7b0,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* N'Ko */
      {0x7c0,0x7ca,0x7e8,0x7eb,0x7f6,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Thaana */
      {0x780,0x798,0x7a5,0x7a6,0x7b0,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Devanagari */
      {0x926,0x947,0x935,0x928,0x93e,0x917,0x930,0x940,'.',0}, 1,
          { { 0, 9, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Bengali */
      {0x9ac,0x9be,0x982,0x9b2,0x9be,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Gurmukhi */
      {0xa17,0xa41,0xa30,0xa2e,0xa41,0xa16,0xa40,'.',0}, 1,
          { { 0, 8, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Gujarati */
      {0xa97,0xac1,0xa9c,0xab0,0xabe,0xaa4,0xac0,'.',0}, 1,
          { { 0, 8, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Oriya */
      {0xb13,0xb21,0xb3c,0xb3f,0xb06,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Tamil */
      {0xba4,0xbae,0xbbf,0xbb4,0xbcd,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Telugu */
      {0xc24,0xc46,0xc32,0xc41,0xc17,0xc41,'.',0}, 1,
          { { 0, 7, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Kannada */
      {0xc95,0xca8,0xccd,0xca8,0xca1,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Malayalam */
      {0xd2e,0xd32,0xd2f,0xd3e,0xd33,0xd02,'.',0}, 1,
          { { 0, 7, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Sinhala */
      {0xd82,0xd85,0xd9a,0xdcf,'.',0}, 1,
          { { 0, 5, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Thai */
      {0x0e04,0x0e27,0x0e32,0x0e21,0x0e1e,0x0e22,0x0e32,0x0e22,0x0e32,0x0e21,
       0x0e2d,0x0e22,0x0e39,0x0e48,0x0e17,0x0e35,0x0e48,0x0e44,0x0e2b,0x0e19,
       0x0e04,0x0e27,0x0e32,0x0e21,0x0e2a, 0x0e33,0x0e40,0x0e23,0x0e47,0x0e08,
       0x0e2d,0x0e22,0x0e39,0x0e48,0x0e17,0x0e35,0x0e48,0x0e19,0x0e31,0x0e48,0x0e19,'.',0}, 1,
          { { 0, 42, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Lao */
      {0xead,0xeb1,0xe81,0xeaa,0xead,0xe99,0xea5,0xeb2,0xea7,'.',0}, 1,
          { { 0, 10, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Tibetan */
      {0xf04,0xf05,0xf0e,0x020,0xf51,0xf7c,0xf53,0xf0b,0xf5a,0xf53,0xf0b,
       0xf51,0xf44,0xf0b,0xf54,0xf7c,0xf0d,'.',0}, 1,
          { { 0, 18, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Myanmar */
      {0x1019,0x103c,0x1014,0x103a,0x1019,0x102c,0x1021,0x1000,0x1039,0x1001,0x101b,0x102c,'.',0}, 1,
          { { 0, 13, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Georgian */
      {0x10a0,0x10d0,0x10da,0x10f1,0x10fb,0x2d00,'.',0}, 1,
          { { 0, 7, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Hangul */
      {0x1100,0x1110,0x1160,0x1170,0x11a8,'.',0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Ethiopic */
      {0x130d,0x12d5,0x12dd,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Cherokee */
      {0x13e3,0x13b3,0x13a9,0x0020,0x13a6,0x13ec,0x13c2,0x13af,0x13cd,0x13d7,0}, 1,
          { { 0, 10, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Canadian */
      {0x1403,0x14c4,0x1483,0x144e,0x1450,0x1466,0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Ogham */
      {0x169b,0x1691,0x168c,0x1690,0x168b,0x169c,0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Runic */
      {0x16a0,0x16a1,0x16a2,0x16a3,0x16a4,0x16a5,0}, 1,
          { { 0, 6, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Khmer */
      {0x1781,0x17c1,0x1798,0x179a,0x1797,0x17b6,0x179f,0x17b6,0x19e0,0}, 1,
          { { 0, 9, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Mongolian */
      {0x182e,0x1823,0x1829,0x182d,0x1823,0x182f,0x0020,0x182a,0x1822,0x1834,0x1822,0x182d,0x180c,0}, 1,
          { { 0, 13, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Limbu */
      {0x1900,0x1910,0x1920,0x1930,0}, 1,
          { { 0, 4, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Tai Le */
      {0x1956,0x196d,0x1970,0x1956,0x196c,0x1973,0x1951,0x1968,0x1952,0x1970,0}, 1,
          { { 0, 10, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* New Tai Lue */
      {0x1992,0x19c4,0}, 1,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Buginese */
      {0x1a00,0x1a10,0}, 1,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Tai Tham */
      {0x1a20,0x1a40,0x1a50,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Balinese */
      {0x1b00,0x1b05,0x1b20,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Sundanese */
      {0x1b80,0x1b85,0x1ba0,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Batak */
      {0x1bc0,0x1be5,0x1bfc,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Lepcha */
      {0x1c00,0x1c20,0x1c40,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Ol Chiki */
      {0x1c50,0x1c5a,0x1c77,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Sundanese Supplement */
      {0x1cc0,0x1cc5,0x1cc7,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Phonetic Extensions */
      {0x1d00,0x1d40,0x1d70,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Combining diacritical marks */
      {0x1dc0,0x300,0x1ddf,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Latin Extended Additional, Extended-C */
      {0x1e00,0x1d00,0x2c60,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Greek Extended */
      {0x3f0,0x1f00,0}, 1,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* General Punctuation */
      {0x1dc0,0x2000,0}, 1,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Superscripts and Subscripts */
      {0x2070,0x2086,0x2000,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Currency, Combining Diacritical Marks for Symbols. Letterlike Symbols.. */
      {0x20a0,0x20b8,0x2000,0x20d0,0x2100,0x2150,0x2190,0x2200,0x2300,0x2400,0x2440,0x2460,0x2500,0x2580,0x25a0,0x2600,
       0x2700,0x27c0,0x27f0,0x2900,0x2980,0x2a00,0x2b00,0}, 1,
          { { 0, 23, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Braille */
      {0x2800,0x2070,0x2000,0}, 1,
          { { 0, 3, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Glagolitic */
      {0x2c00,0x2c12,0}, 1,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    {
      /* Coptic */
      {0x2c80,0x3e2,0x1f00,0}, 2,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT },
            { 2, 1, DWRITE_SCRIPT_SHAPES_DEFAULT } }
    },
    {
      /* Tifinagh */
      {0x2d30,0x2d4a,0}, 1,
          { { 0, 2, DWRITE_SCRIPT_SHAPES_DEFAULT }}
    },
    /* keep this as end marker */
    { {0} }
};

static void init_expected_sa(struct call_sequence **seq, const struct sa_test *test)
{
    static const struct call_entry end_of_sequence = { LastKind };
    int i;

    flush_sequence(seq, 0);

    /* add expected calls */
    for (i = 0; i < test->item_count; i++)
    {
        struct call_entry call;

        call.kind = ScriptAnalysis;
        call.sa.pos = test->sa[i].pos;
        call.sa.len = test->sa[i].len;
        call.sa.shapes = test->sa[i].shapes;
        add_call(seq, 0, &call);
    }

    /* and stop marker */
    add_call(seq, 0, &end_of_sequence);
}

static void get_script_analysis(const WCHAR *str, DWRITE_SCRIPT_ANALYSIS *sa)
{
    IDWriteTextAnalyzer *analyzer;
    HRESULT hr;

    g_source = str;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteTextAnalyzer_AnalyzeScript(analyzer, &analysissource, 0, lstrlenW(g_source), &analysissink2);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    *sa = g_sa;
}

static void test_AnalyzeScript(void)
{
    const struct sa_test *ptr = sa_tests;
    IDWriteTextAnalyzer *analyzer;
    HRESULT hr;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    while (*ptr->string)
    {
        g_source = ptr->string;

        init_expected_sa(expected_seq, ptr);
        hr = IDWriteTextAnalyzer_AnalyzeScript(analyzer, &analysissource, 0, lstrlenW(g_source), &analysissink);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok_sequence(sequences, ANALYZER_ID, expected_seq[0]->sequence, wine_dbgstr_w(ptr->string), FALSE);
        ptr++;
    }

    IDWriteTextAnalyzer_Release(analyzer);
}

struct linebreaks_test {
    const WCHAR text[BREAKPOINT_COUNT+1];
    DWRITE_LINE_BREAKPOINT bp[BREAKPOINT_COUNT];
};

static struct linebreaks_test linebreaks_tests[] = {
    { {'A','-','B',' ','C',0x58a,'D',0x2010,'E',0x2012,'F',0x2013,'\t',0},
      {
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_CAN_BREAK,     FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_CAN_BREAK,     DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_CAN_BREAK,     TRUE,  FALSE },
          { DWRITE_BREAK_CONDITION_CAN_BREAK,     DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_CAN_BREAK,     FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_CAN_BREAK,     DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_CAN_BREAK,     FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_CAN_BREAK,     DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_CAN_BREAK,     FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_CAN_BREAK,     DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, FALSE, FALSE },
          { DWRITE_BREAK_CONDITION_MAY_NOT_BREAK, DWRITE_BREAK_CONDITION_CAN_BREAK,     TRUE,  FALSE }
      }
    },
    { { 0 } }
};

static void compare_breakpoints(const struct linebreaks_test *test, DWRITE_LINE_BREAKPOINT *actual)
{
    const WCHAR *text = test->text;
    int cmp = memcmp(test->bp, actual, sizeof(*actual)*BREAKPOINT_COUNT);
    ok(!cmp, "%s: got wrong breakpoint data\n", wine_dbgstr_w(test->text));
    if (cmp) {
        int i = 0;
        while (*text) {
            ok(!memcmp(&test->bp[i], &actual[i], sizeof(*actual)),
                "%s: got (%d, %d, %d, %d), expected (%d, %d, %d, %d)\n",
                wine_dbgstr_wn(&test->text[i], 1),
                g_actual_bp[i].breakConditionBefore,
                g_actual_bp[i].breakConditionAfter,
                g_actual_bp[i].isWhitespace,
                g_actual_bp[i].isSoftHyphen,
                test->bp[i].breakConditionBefore,
                test->bp[i].breakConditionAfter,
                test->bp[i].isWhitespace,
                test->bp[i].isSoftHyphen);
            text++;
            i++;
        }
    }
}

static void test_AnalyzeLineBreakpoints(void)
{
    static const WCHAR emptyW[] = {0};
    const struct linebreaks_test *ptr = linebreaks_tests;
    IDWriteTextAnalyzer *analyzer;
    HRESULT hr;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    g_source = emptyW;
    hr = IDWriteTextAnalyzer_AnalyzeLineBreakpoints(analyzer, &analysissource, 0, 0, &analysissink);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    while (*ptr->text)
    {
        g_source = ptr->text;

        memset(g_actual_bp, 0, sizeof(g_actual_bp));
        hr = IDWriteTextAnalyzer_AnalyzeLineBreakpoints(analyzer, &analysissource, 0, lstrlenW(g_source), &analysissink);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        compare_breakpoints(ptr, g_actual_bp);

        ptr++;
    }

    IDWriteTextAnalyzer_Release(analyzer);
}

static void test_GetScriptProperties(void)
{
    IDWriteTextAnalyzer1 *analyzer1;
    IDWriteTextAnalyzer *analyzer;
    DWRITE_SCRIPT_ANALYSIS sa;
    DWRITE_SCRIPT_PROPERTIES props;
    HRESULT hr;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteTextAnalyzer_QueryInterface(analyzer, &IID_IDWriteTextAnalyzer1, (void**)&analyzer1);
    IDWriteTextAnalyzer_Release(analyzer);
    if (hr != S_OK) {
        win_skip("GetScriptProperties() is not supported.\n");
        return;
    }

    sa.script = 1000;
    hr = IDWriteTextAnalyzer1_GetScriptProperties(analyzer1, sa, &props);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

if (0) /* crashes on native */
    hr = IDWriteTextAnalyzer1_GetScriptProperties(analyzer1, sa, NULL);

    sa.script = 0;
    hr = IDWriteTextAnalyzer1_GetScriptProperties(analyzer1, sa, &props);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteTextAnalyzer1_Release(analyzer1);
}

struct textcomplexity_test {
    const WCHAR text[5];
    UINT32 length;
    BOOL simple;
    UINT32 len_read;
};

static const struct textcomplexity_test textcomplexity_tests[] = {
    { {0},                     1, FALSE, 1 },
    { {0},                     0,  TRUE, 0 },
    { {0x610,0},               0,  TRUE, 0 },
    { {'A','B','C','D',0},     3,  TRUE, 3 },
    { {'A','B','C','D',0},     5,  TRUE, 4 },
    { {'A','B','C','D',0},    10,  TRUE, 4 },
    { {'A',0x610,'C','D',0},   1,  TRUE, 1 },
    { {'A',0x610,'C','D',0},   2, FALSE, 2 },
    { {0x610,'A','C','D',0},   1, FALSE, 1 },
    { {0x610,'A','C','D',0},   2, FALSE, 1 },
    { {0x610,0x610,'C','D',0}, 2, FALSE, 2 },
    { {0xd800,'A','B',0},      1, FALSE, 1 },
    { {0xd800,'A','B',0},      2, FALSE, 1 },
    { {0xdc00,'A','B',0},      2, FALSE, 1 }
};

static void test_GetTextComplexity(void)
{
    static const WCHAR textW[] = {'A','B','C',0};
    IDWriteTextAnalyzer1 *analyzer1;
    IDWriteTextAnalyzer *analyzer;
    IDWriteFontFace *fontface;
    UINT16 indices[10];
    BOOL simple;
    HRESULT hr;
    UINT32 len;
    int i;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteTextAnalyzer_QueryInterface(analyzer, &IID_IDWriteTextAnalyzer1, (void**)&analyzer1);
    IDWriteTextAnalyzer_Release(analyzer);
    if (hr != S_OK) {
        win_skip("GetTextComplexity() is not supported.\n");
        return;
    }

if (0) { /* crashes on native */
    hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, NULL, 0, NULL, NULL, NULL, NULL);
    hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, NULL, 0, NULL, NULL, &len, NULL);
    hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, textW, 3, NULL, NULL, NULL, NULL);
    hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, textW, 3, NULL, NULL, &len, NULL);
    hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, textW, 3, NULL, &simple, NULL, NULL);
}

    len = 1;
    simple = TRUE;
    hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, NULL, 0, NULL, &simple, &len, NULL);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(len == 0, "got %d\n", len);
    ok(simple == FALSE, "got %d\n", simple);

    len = 1;
    simple = TRUE;
    indices[0] = 1;
    hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, textW, 3, NULL, &simple, &len, NULL);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(len == 0, "got %d\n", len);
    ok(simple == FALSE, "got %d\n", simple);
    ok(indices[0] == 1, "got %d\n", indices[0]);

    fontface = create_fontface();

    for (i = 0; i < sizeof(textcomplexity_tests)/sizeof(struct textcomplexity_test); i++) {
       const struct textcomplexity_test *ptr = &textcomplexity_tests[i];
       len = 1;
       simple = !ptr->simple;
       indices[0] = 0;
       hr = IDWriteTextAnalyzer1_GetTextComplexity(analyzer1, ptr->text, ptr->length, fontface, &simple, &len, indices);
       ok(hr == S_OK, "%d: got 0x%08x\n", i, hr);
       ok(len == ptr->len_read, "%d: read length: got %d, expected %d\n", i, len, ptr->len_read);
       ok(simple == ptr->simple, "%d: simple: got %d, expected %d\n", i, simple, ptr->simple);
       if (simple && ptr->length)
           ok(indices[0] > 0, "%d: got %d\n", i, indices[0]);
       else
           ok(indices[0] == 0, "%d: got %d\n", i, indices[0]);
    }

    IDWriteFontFace_Release(fontface);
    IDWriteTextAnalyzer1_Release(analyzer1);
}

static void test_numbersubstitution(void)
{
    static const WCHAR dummyW[] = {'d','u','m','m','y',0};
    IDWriteNumberSubstitution *substitution;
    HRESULT hr;

    hr = IDWriteFactory_CreateNumberSubstitution(factory, DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE, NULL, FALSE, &substitution);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteNumberSubstitution_Release(substitution);

    /* invalid method */
    hr = IDWriteFactory_CreateNumberSubstitution(factory, DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL+1, NULL, FALSE, &substitution);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    /* invalid method */
    hr = IDWriteFactory_CreateNumberSubstitution(factory, -1, NULL, FALSE, &substitution);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    /* invalid locale */
    hr = IDWriteFactory_CreateNumberSubstitution(factory, DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL, dummyW, FALSE, &substitution);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateNumberSubstitution(factory, DWRITE_NUMBER_SUBSTITUTION_METHOD_CONTEXTUAL, dummyW, FALSE, &substitution);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateNumberSubstitution(factory, DWRITE_NUMBER_SUBSTITUTION_METHOD_NATIONAL, dummyW, FALSE, &substitution);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    /* invalid locale, but it's not needed for this method */
    hr = IDWriteFactory_CreateNumberSubstitution(factory, DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE, dummyW, FALSE, &substitution);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteNumberSubstitution_Release(substitution);
}

static void test_GetGlyphs(void)
{
    static const WCHAR test1W[] = {'<','B',' ','C',0};
    static const WCHAR test2W[] = {'<','B','\t','C',0};
    DWRITE_SHAPING_GLYPH_PROPERTIES shapingprops[20];
    DWRITE_SHAPING_TEXT_PROPERTIES props[20];
    UINT32 maxglyphcount, actual_count;
    IDWriteTextAnalyzer *analyzer;
    IDWriteFontFace *fontface;
    DWRITE_SCRIPT_ANALYSIS sa;
    UINT16 clustermap[10];
    UINT16 glyphs1[10];
    UINT16 glyphs2[10];
    HRESULT hr;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    fontface = create_fontface();

    maxglyphcount = 1;
    sa.script = 0;
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, test1W, lstrlenW(test1W), fontface, FALSE, FALSE, &sa, NULL,
        NULL, NULL, NULL, 0, maxglyphcount, clustermap, props, glyphs1, shapingprops, &actual_count);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);

if (0) {
    /* NULL fontface - crashes on Windows */
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, test1W, lstrlenW(test1W), NULL, FALSE, FALSE, &sa, NULL,
        NULL, NULL, NULL, 0, maxglyphcount, clustermap, props, glyphs1, shapingprops, &actual_count);
}

    /* invalid script id */
    maxglyphcount = 10;
    actual_count = 0;
    sa.script = 999;
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, test1W, lstrlenW(test1W), fontface, FALSE, FALSE, &sa, NULL,
        NULL, NULL, NULL, 0, maxglyphcount, clustermap, props, glyphs1, shapingprops, &actual_count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(actual_count == 4, "got %d\n", actual_count);
    ok(sa.script == 999, "got %u\n", sa.script);

    /* no '\t' -> ' ' replacement */
    maxglyphcount = 10;
    actual_count = 0;
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, test1W, lstrlenW(test1W), fontface, FALSE, FALSE, &sa, NULL,
        NULL, NULL, NULL, 0, maxglyphcount, clustermap, props, glyphs1, shapingprops, &actual_count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(actual_count == 4, "got %d\n", actual_count);

    actual_count = 0;
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, test2W, lstrlenW(test2W), fontface, FALSE, FALSE, &sa, NULL,
        NULL, NULL, NULL, 0, maxglyphcount, clustermap, props, glyphs2, shapingprops, &actual_count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(actual_count == 4, "got %d\n", actual_count);
    ok(glyphs1[2] != glyphs2[2], "got %d\n", glyphs1[2]);

    /* check that mirroring works */
    maxglyphcount = 10;
    actual_count = 0;
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, test1W, lstrlenW(test1W), fontface, FALSE, FALSE, &sa, NULL,
        NULL, NULL, NULL, 0, maxglyphcount, clustermap, props, glyphs1, shapingprops, &actual_count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(actual_count == 4, "got %d\n", actual_count);

    actual_count = 0;
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, test1W, lstrlenW(test1W), fontface, FALSE, TRUE, &sa, NULL,
        NULL, NULL, NULL, 0, maxglyphcount, clustermap, props, glyphs2, shapingprops, &actual_count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(actual_count == 4, "got %d\n", actual_count);
    ok(glyphs1[0] != glyphs2[0], "got %d\n", glyphs1[0]);

    IDWriteTextAnalyzer_Release(analyzer);
    IDWriteFontFace_Release(fontface);
}

static BOOL has_feature(const DWRITE_FONT_FEATURE_TAG *tags, UINT32 count, DWRITE_FONT_FEATURE_TAG feature)
{
    UINT32 i;

    for (i = 0; i < count; i++)
        if (tags[i] == feature) return TRUE;
    return FALSE;
}

static void test_GetTypographicFeatures(void)
{
    static const WCHAR localeW[] = {'c','a','d','a','b','r','a',0};
    static const WCHAR arabicW[] = {0x064a,0x064f,0x0633,0};
    static const WCHAR abcW[] = {'a','b','c',0};
    DWRITE_FONT_FEATURE_TAG tags[20];
    IDWriteTextAnalyzer2 *analyzer2;
    IDWriteTextAnalyzer *analyzer;
    IDWriteFontFace *fontface;
    DWRITE_SCRIPT_ANALYSIS sa;
    UINT32 count;
    HRESULT hr;
    BOOL ret;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteTextAnalyzer_QueryInterface(analyzer, &IID_IDWriteTextAnalyzer2, (void**)&analyzer2);
    IDWriteTextAnalyzer_Release(analyzer);
    if (hr != S_OK) {
        win_skip("GetTypographicFeatures() is not supported.\n");
        return;
    }

    fontface = create_fontface();

    get_script_analysis(abcW, &sa);
    count = 0;
    hr = IDWriteTextAnalyzer2_GetTypographicFeatures(analyzer2, fontface, sa, NULL, 0, &count, NULL);
todo_wine {
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(count > 0, "got %u\n", count);
}
    /* invalid locale name is ignored */
    get_script_analysis(abcW, &sa);
    count = 0;
    hr = IDWriteTextAnalyzer2_GetTypographicFeatures(analyzer2, fontface, sa, localeW, 0, &count, NULL);
todo_wine {
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(count > 0, "got %u\n", count);
}
    /* both GSUB and GPOS features are reported */
    get_script_analysis(arabicW, &sa);
    memset(tags, 0, sizeof(tags));
    count = 0;
    hr = IDWriteTextAnalyzer2_GetTypographicFeatures(analyzer2, fontface, sa, NULL, sizeof(tags)/sizeof(tags[0]), &count, tags);
    ok(hr == S_OK, "got 0x%08x\n", hr);
todo_wine {
    ok(count > 0, "got %u\n", count);
    ret = has_feature(tags, count, DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES);
    ok(ret, "expected 'calt' feature\n");
    ret = has_feature(tags, count, DWRITE_FONT_FEATURE_TAG_MARK_TO_MARK_POSITIONING);
    ok(ret, "expected 'mkmk' feature\n");
}
    get_script_analysis(abcW, &sa);
    memset(tags, 0, sizeof(tags));
    count = 0;
    hr = IDWriteTextAnalyzer2_GetTypographicFeatures(analyzer2, fontface, sa, NULL, sizeof(tags)/sizeof(tags[0]), &count, tags);
    ok(hr == S_OK, "got 0x%08x\n", hr);
todo_wine {
    ok(count > 0, "got %u\n", count);
    ret = has_feature(tags, count, DWRITE_FONT_FEATURE_TAG_GLYPH_COMPOSITION_DECOMPOSITION);
    ok(ret, "expected 'ccmp' feature\n");
    ret = has_feature(tags, count, DWRITE_FONT_FEATURE_TAG_MARK_TO_MARK_POSITIONING);
    ok(ret, "expected 'mkmk' feature\n");
}
    ret = has_feature(tags, count, DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES);
    ok(!ret, "unexpected 'calt' feature\n");

    IDWriteFontFace_Release(fontface);
    IDWriteTextAnalyzer2_Release(analyzer2);
}

static void test_GetGlyphPlacements(void)
{
    DWRITE_SHAPING_GLYPH_PROPERTIES glyphprops[2];
    DWRITE_SHAPING_TEXT_PROPERTIES textprops[2];
    static const WCHAR aW[] = {'A','D',0};
    UINT16 clustermap[2], glyphs[2];
    DWRITE_GLYPH_OFFSET offsets[2];
    IDWriteTextAnalyzer *analyzer;
    IDWriteFontFace *fontface;
    DWRITE_SCRIPT_ANALYSIS sa;
    FLOAT advances[2];
    UINT32 count, len;
    HRESULT hr;

    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    create_testfontfile(test_fontfile);
    fontface = create_testfontface(test_fontfile);

    get_script_analysis(aW, &sa);
    count = 0;
    len = lstrlenW(aW);
    hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, aW, len, fontface, FALSE, FALSE, &sa, NULL,
        NULL, NULL, NULL, 0, len, clustermap, textprops, glyphs, glyphprops, &count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(count == 2, "got %u\n", count);

    /* just return on zero glyphs */
    advances[0] = advances[1] = 1.0;
    offsets[0].advanceOffset = offsets[0].ascenderOffset = 2.0;
    hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, aW, clustermap, textprops,
        len, glyphs, glyphprops, 0, fontface, 0.0, FALSE, FALSE, &sa, NULL, NULL,
        NULL, 0, advances, offsets);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advances[0] == 1.0, "got %.2f\n", advances[0]);
    ok(offsets[0].advanceOffset == 2.0 && offsets[0].ascenderOffset == 2.0, "got %.2f,%.2f\n",
        offsets[0].advanceOffset, offsets[0].ascenderOffset);

    /* advances/offsets are scaled with provided font emSize and designed eM box size */
    advances[0] = advances[1] = 1.0;
    memset(offsets, 0xcc, sizeof(offsets));
    hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, aW, clustermap, textprops,
        len, glyphs, glyphprops, len, fontface, 0.0, FALSE, FALSE, &sa, NULL, NULL,
        NULL, 0, advances, offsets);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advances[0] == 0.0, "got %.2f\n", advances[0]);
    ok(offsets[0].advanceOffset == 0.0 && offsets[0].ascenderOffset == 0.0, "got %.2f,%.2f\n",
        offsets[0].advanceOffset, offsets[0].ascenderOffset);

    advances[0] = advances[1] = 1.0;
    memset(offsets, 0xcc, sizeof(offsets));
    hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, aW, clustermap, textprops,
        len, glyphs, glyphprops, len, fontface, 2048.0, FALSE, FALSE, &sa, NULL, NULL,
        NULL, 0, advances, offsets);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advances[0] == 1000.0, "got %.2f\n", advances[0]);
    ok(offsets[0].advanceOffset == 0.0 && offsets[0].ascenderOffset == 0.0, "got %.2f,%.2f\n",
        offsets[0].advanceOffset, offsets[0].ascenderOffset);

    advances[0] = advances[1] = 1.0;
    memset(offsets, 0xcc, sizeof(offsets));
    hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, aW, clustermap, textprops,
        len, glyphs, glyphprops, len, fontface, 1024.0, FALSE, FALSE, &sa, NULL, NULL,
        NULL, 0, advances, offsets);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advances[0] == 500.0, "got %.2f\n", advances[0]);
    ok(advances[1] == 500.0, "got %.2f\n", advances[1]);
    ok(offsets[0].advanceOffset == 0.0 && offsets[0].ascenderOffset == 0.0, "got %.2f,%.2f\n",
        offsets[0].advanceOffset, offsets[0].ascenderOffset);

    advances[0] = advances[1] = 1.0;
    memset(offsets, 0xcc, sizeof(offsets));
    hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, aW, clustermap, textprops,
        len, glyphs, glyphprops, len, fontface, 20.48, FALSE, FALSE, &sa, NULL, NULL,
        NULL, 0, advances, offsets);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advances[0] == 10.0, "got %.2f\n", advances[0]);
    ok(advances[1] == 10.0, "got %.2f\n", advances[1]);
    ok(offsets[0].advanceOffset == 0.0 && offsets[0].ascenderOffset == 0.0, "got %.2f,%.2f\n",
        offsets[0].advanceOffset, offsets[0].ascenderOffset);

    /* without clustermap */
    advances[0] = advances[1] = 1.0;
    hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, aW, NULL, textprops,
        len, glyphs, glyphprops, len, fontface, 1024.0, FALSE, FALSE, &sa, NULL, NULL,
        NULL, 0, advances, offsets);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advances[0] == 500.0, "got %.2f\n", advances[0]);
    ok(advances[1] == 500.0, "got %.2f\n", advances[1]);

    /* it's happy to use negative size too */
    advances[0] = advances[1] = 1.0;
    memset(offsets, 0xcc, sizeof(offsets));
    hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, aW, clustermap, textprops,
        len, glyphs, glyphprops, len, fontface, -10.24, FALSE, FALSE, &sa, NULL, NULL,
        NULL, 0, advances, offsets);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advances[0] == -5.0, "got %.2f\n", advances[0]);
    ok(offsets[0].advanceOffset == 0.0 && offsets[0].ascenderOffset == 0.0, "got %.2f,%.2f\n",
        offsets[0].advanceOffset, offsets[0].ascenderOffset);

    IDWriteTextAnalyzer_Release(analyzer);
    DeleteFileW(test_fontfile);
}

START_TEST(analyzer)
{
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown**)&factory);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    if (hr != S_OK)
    {
        win_skip("failed to create factory\n");
        return;
    }

    init_call_sequences(sequences, NUM_CALL_SEQUENCES);
    init_call_sequences(expected_seq, 1);

    test_AnalyzeScript();
    test_AnalyzeLineBreakpoints();
    test_GetScriptProperties();
    test_GetTextComplexity();
    test_GetGlyphs();
    test_numbersubstitution();
    test_GetTypographicFeatures();
    test_GetGlyphPlacements();

    IDWriteFactory_Release(factory);
}
