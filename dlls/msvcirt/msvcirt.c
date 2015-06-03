/*
 * Copyright (C) 2007 Alexandre Julliard
 * Copyright (C) 2015 Iván Matellanes
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

#include <stdarg.h>
#include <stdio.h>

#include "msvcirt.h"
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msvcirt);

/* class streambuf */
typedef struct {
    const vtable_ptr *vtable;
    int allocated;
    int unbuffered;
    int unknown;
    char *base;
    char *ebuf;
    char *pbase;
    char *pptr;
    char *epptr;
    char *eback;
    char *gptr;
    char *egptr;
    int unknown2;
    CRITICAL_SECTION lock;
} streambuf;

streambuf* __thiscall streambuf_setbuf(streambuf*, char*, int);
void __thiscall streambuf_setg(streambuf*, char*, char*, char*);
void __thiscall streambuf_setp(streambuf*, char*, char*);

typedef struct {
    LPVOID VTable;
} class_ios;

typedef struct {
    LPVOID VTable;
} class_ostream;

typedef struct {
    LPVOID VTable;
} class_strstreambuf;

/* ??_7streambuf@@6B@ */
extern const vtable_ptr MSVCP_streambuf_vtable;

#ifndef __GNUC__
void __asm_dummy_vtables(void) {
#endif
    __ASM_VTABLE(streambuf,
            VTABLE_ADD_FUNC(streambuf_vector_dtor)
            VTABLE_ADD_FUNC(streambuf_sync)
            VTABLE_ADD_FUNC(streambuf_setbuf)
            VTABLE_ADD_FUNC(streambuf_seekoff)
            VTABLE_ADD_FUNC(streambuf_seekpos)
            VTABLE_ADD_FUNC(streambuf_xsputn)
            VTABLE_ADD_FUNC(streambuf_xsgetn)
            VTABLE_ADD_FUNC(streambuf_overflow)
            VTABLE_ADD_FUNC(streambuf_underflow)
            VTABLE_ADD_FUNC(streambuf_pbackfail)
            VTABLE_ADD_FUNC(streambuf_doallocate));
#ifndef __GNUC__
}
#endif

DEFINE_RTTI_DATA0(streambuf, 0, ".?AVstreambuf@@")

/* ??0streambuf@@IAE@PADH@Z */
/* ??0streambuf@@IEAA@PEADH@Z */
DEFINE_THISCALL_WRAPPER(streambuf_reserve_ctor, 12)
streambuf* __thiscall streambuf_reserve_ctor(streambuf *this, char *buffer, int length)
{
    TRACE("(%p %p %d)\n", this, buffer, length);
    this->vtable = &MSVCP_streambuf_vtable;
    this->allocated = 0;
    this->unknown = -1;
    this->unknown2 = -1;
    this->base = NULL;
    streambuf_setbuf(this, buffer, length);
    streambuf_setg(this, NULL, NULL, NULL);
    streambuf_setp(this, NULL, NULL);
    InitializeCriticalSection(&this->lock);
    return this;
}

/* ??0streambuf@@IAE@XZ */
/* ??0streambuf@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(streambuf_ctor, 4)
streambuf* __thiscall streambuf_ctor(streambuf *this)
{
    streambuf_reserve_ctor(this, NULL, 0);
    this->unbuffered = 0;
    return this;
}

/* ??0streambuf@@QAE@ABV0@@Z */
/* ??0streambuf@@QEAA@AEBV0@@Z */
DEFINE_THISCALL_WRAPPER(streambuf_copy_ctor, 8)
streambuf* __thiscall streambuf_copy_ctor(streambuf *this, const streambuf *copy)
{
    TRACE("(%p %p)\n", this, copy);
    *this = *copy;
    this->vtable = &MSVCP_streambuf_vtable;
    return this;
}

/* ??1streambuf@@UAE@XZ */
/* ??1streambuf@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(streambuf_dtor, 4)
void __thiscall streambuf_dtor(streambuf *this)
{
    TRACE("(%p)\n", this);
    if (this->allocated)
        MSVCRT_operator_delete(this->base);
    DeleteCriticalSection(&this->lock);
}

/* ??4streambuf@@QAEAAV0@ABV0@@Z */
/* ??4streambuf@@QEAAAEAV0@AEBV0@@Z */
DEFINE_THISCALL_WRAPPER(streambuf_assign, 8)
streambuf* __thiscall streambuf_assign(streambuf *this, const streambuf *rhs)
{
    streambuf_dtor(this);
    return streambuf_copy_ctor(this, rhs);
}

/* ??_Estreambuf@@UAEPAXI@Z */
DEFINE_THISCALL_WRAPPER(streambuf_vector_dtor, 8)
streambuf* __thiscall streambuf_vector_dtor(streambuf *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    if (flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        INT_PTR i, *ptr = (INT_PTR *)this-1;

        for (i = *ptr-1; i >= 0; i--)
            streambuf_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        streambuf_dtor(this);
        if (flags & 1)
            MSVCRT_operator_delete(this);
    }
    return this;
}

/* ??_Gstreambuf@@UAEPAXI@Z */
DEFINE_THISCALL_WRAPPER(streambuf_scalar_dtor, 8)
streambuf* __thiscall streambuf_scalar_dtor(streambuf *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    streambuf_dtor(this);
    if (flags & 1) MSVCRT_operator_delete(this);
    return this;
}

/* ?doallocate@streambuf@@MAEHXZ */
/* ?doallocate@streambuf@@MEAAHXZ */
DEFINE_THISCALL_WRAPPER(streambuf_doallocate, 4)
int __thiscall streambuf_doallocate(streambuf *this)
{
    FIXME("(%p): stub\n", this);
    return EOF;
}

/* Unexported */
DEFINE_THISCALL_WRAPPER(streambuf_overflow, 8)
int __thiscall streambuf_overflow(streambuf *this, int c)
{
    return EOF;
}

/* ?pbackfail@streambuf@@UAEHH@Z */
/* ?pbackfail@streambuf@@UEAAHH@Z */
DEFINE_THISCALL_WRAPPER(streambuf_pbackfail, 8)
int __thiscall streambuf_pbackfail(streambuf *this, int c)
{
    FIXME("(%p %d): stub\n", this, c);
    return 0;
}

/* ?seekoff@streambuf@@UAEJJW4seek_dir@ios@@H@Z */
/* ?seekoff@streambuf@@UEAAJJW4seek_dir@ios@@H@Z */
DEFINE_THISCALL_WRAPPER(streambuf_seekoff, 16)
streampos __thiscall streambuf_seekoff(streambuf *this, streamoff offset, int dir, int mode)
{
    FIXME("(%p %d %d %d): stub\n", this, offset, dir, mode);
    return EOF;
}

/* ?seekpos@streambuf@@UAEJJH@Z */
/* ?seekpos@streambuf@@UEAAJJH@Z */
DEFINE_THISCALL_WRAPPER(streambuf_seekpos, 12)
streampos __thiscall streambuf_seekpos(streambuf *this, streampos pos, int mode)
{
    FIXME("(%p %d %d): stub\n", this, pos, mode);
    return EOF;
}

/* ?setb@streambuf@@IAEXPAD0H@Z */
/* ?setb@streambuf@@IEAAXPEAD0H@Z */
DEFINE_THISCALL_WRAPPER(streambuf_setb, 16)
void __thiscall streambuf_setb(streambuf *this, char *ba, char *eb, int delete)
{
    TRACE("(%p %p %p %d)\n", this, ba, eb, delete);
    if (this->allocated)
        MSVCRT_operator_delete(this->base);
    this->allocated = delete;
    this->base = ba;
    this->ebuf = eb;
}

/* ?setbuf@streambuf@@UAEPAV1@PADH@Z */
/* ?setbuf@streambuf@@UEAAPEAV1@PEADH@Z */
DEFINE_THISCALL_WRAPPER(streambuf_setbuf, 12)
streambuf* __thiscall streambuf_setbuf(streambuf *this, char *buffer, int length)
{
    TRACE("(%p %p %d)\n", this, buffer, length);
    if (this->base != NULL)
        return NULL;

    if (buffer == NULL || !length) {
        this->unbuffered = 1;
        this->base = this->ebuf = NULL;
    } else {
        this->unbuffered = 0;
        this->base = buffer;
        this->ebuf = buffer + length;
    }
    return this;
}

/* ?setg@streambuf@@IAEXPAD00@Z */
/* ?setg@streambuf@@IEAAXPEAD00@Z */
DEFINE_THISCALL_WRAPPER(streambuf_setg, 16)
void __thiscall streambuf_setg(streambuf *this, char *ek, char *gp, char *eg)
{
    TRACE("(%p %p %p %p)\n", this, ek, gp, eg);
    this->eback = ek;
    this->gptr = gp;
    this->egptr = eg;
}

/* ?setp@streambuf@@IAEXPAD0@Z */
/* ?setp@streambuf@@IEAAXPEAD0@Z */
DEFINE_THISCALL_WRAPPER(streambuf_setp, 12)
void __thiscall streambuf_setp(streambuf *this, char *pb, char *ep)
{
    TRACE("(%p %p %p)\n", this, pb, ep);
    this->pbase = this->pptr = pb;
    this->epptr = ep;
}

/* ?sync@streambuf@@UAEHXZ */
/* ?sync@streambuf@@UEAAHXZ */
DEFINE_THISCALL_WRAPPER(streambuf_sync, 4)
int __thiscall streambuf_sync(streambuf *this)
{
    FIXME("(%p): stub\n", this);
    return EOF;
}

/* Unexported */
DEFINE_THISCALL_WRAPPER(streambuf_underflow, 4)
int __thiscall streambuf_underflow(streambuf *this)
{
    return EOF;
}

/* ?xsgetn@streambuf@@UAEHPADH@Z */
/* ?xsgetn@streambuf@@UEAAHPEADH@Z */
DEFINE_THISCALL_WRAPPER(streambuf_xsgetn, 12)
int __thiscall streambuf_xsgetn(streambuf *this, char *buffer, int count)
{
    FIXME("(%p %p %d): stub\n", this, buffer, count);
    return 0;
}

/* ?xsputn@streambuf@@UAEHPBDH@Z */
/* ?xsputn@streambuf@@UEAAHPEBDH@Z */
DEFINE_THISCALL_WRAPPER(streambuf_xsputn, 12)
int __thiscall streambuf_xsputn(streambuf *this, const char *data, int length)
{
    FIXME("(%p %p %d): stub\n", this, data, length);
    return 0;
}

/******************************************************************
 *		 ??1ios@@UAE@XZ (MSVCRTI.@)
 *        class ios & __thiscall ios::-ios<<(void)
 */
DEFINE_THISCALL_WRAPPER(MSVCIRT_ios_sl_void,4)
void * __thiscall MSVCIRT_ios_sl_void(class_ios * _this)
{
   FIXME("(%p) stub\n", _this);
   return _this;
}

/******************************************************************
 *		 ??0ostrstream@@QAE@XZ (MSVCRTI.@)
 *        class ostream & __thiscall ostrstream::ostrstream<<(void)
 */
DEFINE_THISCALL_WRAPPER(MSVCIRT_ostrstream_sl_void,4)
void * __thiscall MSVCIRT_ostrstream_sl_void(class_ostream * _this)
{
   FIXME("(%p) stub\n", _this);
   return _this;
}

/******************************************************************
 *		??6ostream@@QAEAAV0@E@Z (MSVCRTI.@)
 *    class ostream & __thiscall ostream::operator<<(unsigned char)
 */
DEFINE_THISCALL_WRAPPER(MSVCIRT_operator_sl_uchar,8)
void * __thiscall MSVCIRT_operator_sl_uchar(class_ostream * _this, unsigned char ch)
{
   FIXME("(%p)->(%c) stub\n", _this, ch);
   return _this;
}

/******************************************************************
 *		 ??6ostream@@QAEAAV0@H@Z (MSVCRTI.@)
 *        class ostream & __thiscall ostream::operator<<(int)
 */
DEFINE_THISCALL_WRAPPER(MSVCIRT_operator_sl_int,8)
void * __thiscall MSVCIRT_operator_sl_int(class_ostream * _this, int integer)
{
   FIXME("(%p)->(%d) stub\n", _this, integer);
   return _this;
}

/******************************************************************
 *		??6ostream@@QAEAAV0@PBD@Z (MSVCRTI.@)
 *    class ostream & __thiscall ostream::operator<<(char const *)
 */
DEFINE_THISCALL_WRAPPER(MSVCIRT_operator_sl_pchar,8)
void * __thiscall MSVCIRT_operator_sl_pchar(class_ostream * _this, const char * string)
{
   FIXME("(%p)->(%s) stub\n", _this, debugstr_a(string));
   return _this;
}

/******************************************************************
 *		??6ostream@@QAEAAV0@P6AAAV0@AAV0@@Z@Z (MSVCRTI.@)
 *    class ostream & __thiscall ostream::operator<<(class ostream & (__cdecl*)(class ostream &))
 */
DEFINE_THISCALL_WRAPPER(MSVCIRT_operator_sl_callback,8)
void * __thiscall MSVCIRT_operator_sl_callback(class_ostream * _this, class_ostream * (__cdecl*func)(class_ostream*))
{
   TRACE("%p, %p\n", _this, func);
   return func(_this);
}

/******************************************************************
 *		?endl@@YAAAVostream@@AAV1@@Z (MSVCRTI.@)
 *           class ostream & __cdecl endl(class ostream &)
 */
void * CDECL MSVCIRT_endl(class_ostream * _this)
{
   FIXME("(%p)->() stub\n", _this);
   return _this;
}

/******************************************************************
 *		?ends@@YAAAVostream@@AAV1@@Z (MSVCRTI.@)
 *           class ostream & __cdecl ends(class ostream &)
 */
void * CDECL MSVCIRT_ends(class_ostream * _this)
{
   FIXME("(%p)->() stub\n", _this);
   return _this;
}

/******************************************************************
 *		?str@strstreambuf@@QAEPADXZ (MSVCRTI.@)
 *           class strstreambuf & __thiscall strstreambuf::str(class strstreambuf &)
 */
DEFINE_THISCALL_WRAPPER(MSVCIRT_str_sl_void,4)
char * __thiscall MSVCIRT_str_sl_void(class_strstreambuf * _this)
{
   FIXME("(%p)->() stub\n", _this);
   return 0;
}

void (__cdecl *MSVCRT_operator_delete)(void*);

static void init_cxx_funcs(void)
{
    HMODULE hmod = GetModuleHandleA("msvcrt.dll");

    if (sizeof(void *) > sizeof(int))  /* 64-bit has different names */
    {
        MSVCRT_operator_delete = (void*)GetProcAddress(hmod, "??3@YAXPEAX@Z");
    }
    else
    {
        MSVCRT_operator_delete = (void*)GetProcAddress(hmod, "??3@YAXPAX@Z");
    }
}

static void init_io(void *base)
{
#ifdef __x86_64__
    init_streambuf_rtti(base);
#endif
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, LPVOID reserved )
{
   switch (reason)
   {
   case DLL_WINE_PREATTACH:
       return FALSE;  /* prefer native version */
   case DLL_PROCESS_ATTACH:
       init_cxx_funcs();
       init_exception(inst);
       init_io(inst);
       DisableThreadLibraryCalls( inst );
       break;
   }
   return TRUE;
}
