/*
 * Copyright 2015 Iván Matellanes
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

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <windef.h>
#include <winbase.h>
#include "wine/test.h"

typedef void (*vtable_ptr)(void);
typedef int filedesc;

typedef enum {
    IOSTATE_goodbit   = 0x0,
    IOSTATE_eofbit    = 0x1,
    IOSTATE_failbit   = 0x2,
    IOSTATE_badbit    = 0x4
} ios_io_state;

typedef enum {
    OPENMODE_in          = 0x1,
    OPENMODE_out         = 0x2,
    OPENMODE_ate         = 0x4,
    OPENMODE_app         = 0x8,
    OPENMODE_trunc       = 0x10,
    OPENMODE_nocreate    = 0x20,
    OPENMODE_noreplace   = 0x40,
    OPENMODE_binary      = 0x80
} ios_open_mode;

typedef enum {
    FLAGS_skipws     = 0x1,
    FLAGS_left       = 0x2,
    FLAGS_right      = 0x4,
    FLAGS_internal   = 0x8,
    FLAGS_dec        = 0x10,
    FLAGS_oct        = 0x20,
    FLAGS_hex        = 0x40,
    FLAGS_showbase   = 0x80,
    FLAGS_showpoint  = 0x100,
    FLAGS_uppercase  = 0x200,
    FLAGS_showpos    = 0x400,
    FLAGS_scientific = 0x800,
    FLAGS_fixed      = 0x1000,
    FLAGS_unitbuf    = 0x2000,
    FLAGS_stdio      = 0x4000
} ios_flags;

const int filebuf_sh_none = 0x800;
const int filebuf_sh_read = 0xa00;
const int filebuf_sh_write = 0xc00;
const int filebuf_openprot = 420;
const int filebuf_binary = _O_BINARY;
const int filebuf_text = _O_TEXT;

/* class streambuf */
typedef struct {
    const vtable_ptr *vtable;
    int allocated;
    int unbuffered;
    int stored_char;
    char *base;
    char *ebuf;
    char *pbase;
    char *pptr;
    char *epptr;
    char *eback;
    char *gptr;
    char *egptr;
    int do_lock;
    CRITICAL_SECTION lock;
} streambuf;

/* class filebuf */
typedef struct {
    streambuf base;
    filedesc fd;
    int close;
} filebuf;

/* class ios */
struct _ostream;
typedef struct {
    const vtable_ptr *vtable;
    streambuf *sb;
    ios_io_state state;
    int special[4];
    int delbuf;
    struct _ostream *tie;
    ios_flags flags;
    int precision;
    char fill;
    int width;
    int do_lock;
    CRITICAL_SECTION lock;
} ios;

/* class ostream */
typedef struct _ostream {
    const vtable_ptr *vtable;
} ostream;

#undef __thiscall
#ifdef __i386__
#define __thiscall __stdcall
#else
#define __thiscall __cdecl
#endif

static void* (__cdecl *p_operator_new)(unsigned int);

/* streambuf */
static streambuf* (*__thiscall p_streambuf_reserve_ctor)(streambuf*, char*, int);
static streambuf* (*__thiscall p_streambuf_ctor)(streambuf*);
static void (*__thiscall p_streambuf_dtor)(streambuf*);
static int (*__thiscall p_streambuf_allocate)(streambuf*);
static void (*__thiscall p_streambuf_clrclock)(streambuf*);
static int (*__thiscall p_streambuf_doallocate)(streambuf*);
static void (*__thiscall p_streambuf_gbump)(streambuf*, int);
static void (*__thiscall p_streambuf_lock)(streambuf*);
static int (*__thiscall p_streambuf_pbackfail)(streambuf*, int);
static void (*__thiscall p_streambuf_pbump)(streambuf*, int);
static int (*__thiscall p_streambuf_sbumpc)(streambuf*);
static void (*__thiscall p_streambuf_setb)(streambuf*, char*, char*, int);
static void (*__thiscall p_streambuf_setlock)(streambuf*);
static streambuf* (*__thiscall p_streambuf_setbuf)(streambuf*, char*, int);
static int (*__thiscall p_streambuf_sgetc)(streambuf*);
static int (*__thiscall p_streambuf_snextc)(streambuf*);
static int (*__thiscall p_streambuf_sputc)(streambuf*, int);
static void (*__thiscall p_streambuf_stossc)(streambuf*);
static int (*__thiscall p_streambuf_sync)(streambuf*);
static void (*__thiscall p_streambuf_unlock)(streambuf*);
static int (*__thiscall p_streambuf_xsgetn)(streambuf*, char*, int);
static int (*__thiscall p_streambuf_xsputn)(streambuf*, const char*, int);

/* filebuf */
static filebuf* (*__thiscall p_filebuf_fd_ctor)(filebuf*, int);
static filebuf* (*__thiscall p_filebuf_fd_reserve_ctor)(filebuf*, int, char*, int);
static filebuf* (*__thiscall p_filebuf_ctor)(filebuf*);
static void (*__thiscall p_filebuf_dtor)(filebuf*);
static filebuf* (*__thiscall p_filebuf_attach)(filebuf*, filedesc);
static filebuf* (*__thiscall p_filebuf_open)(filebuf*, const char*, ios_open_mode, int);
static filebuf* (*__thiscall p_filebuf_close)(filebuf*);
static int (*__thiscall p_filebuf_setmode)(filebuf*, int);
static streambuf* (*__thiscall p_filebuf_setbuf)(filebuf*, char*, int);

/* ios */
static ios* (*__thiscall p_ios_copy_ctor)(ios*, const ios*);
static ios* (*__thiscall p_ios_ctor)(ios*);
static ios* (*__thiscall p_ios_sb_ctor)(ios*, streambuf*);
static ios* (*__thiscall p_ios_assign)(ios*, const ios*);
static void (*__thiscall p_ios_init)(ios*, streambuf*);
static void (*__thiscall p_ios_dtor)(ios*);
static void (*__cdecl p_ios_clrlock)(ios*);
static void (*__cdecl p_ios_setlock)(ios*);
static void (*__cdecl p_ios_lock)(ios*);
static void (*__cdecl p_ios_unlock)(ios*);
static void (*__cdecl p_ios_lockbuf)(ios*);
static void (*__cdecl p_ios_unlockbuf)(ios*);
static CRITICAL_SECTION *p_ios_static_lock;
static void (*__cdecl p_ios_lockc)(void);
static void (*__cdecl p_ios_unlockc)(void);
static LONG (*__thiscall p_ios_flags_set)(ios*, LONG);
static LONG (*__thiscall p_ios_flags_get)(const ios*);
static LONG (*__thiscall p_ios_setf)(ios*, LONG);
static LONG (*__thiscall p_ios_setf_mask)(ios*, LONG, LONG);
static LONG (*__thiscall p_ios_unsetf)(ios*, LONG);
static int (*__thiscall p_ios_good)(const ios*);
static int (*__thiscall p_ios_bad)(const ios*);
static int (*__thiscall p_ios_eof)(const ios*);
static int (*__thiscall p_ios_fail)(const ios*);
static void (*__thiscall p_ios_clear)(ios*, int);
static LONG *p_ios_maxbit;
static LONG (*__cdecl p_ios_bitalloc)(void);
static int *p_ios_curindex;
static LONG *p_ios_statebuf;
static LONG* (*__thiscall p_ios_iword)(const ios*, int);
static void** (*__thiscall p_ios_pword)(const ios*, int);
static int (*__cdecl p_ios_xalloc)(void);
static int *p_ios_fLockcInit;

/* Emulate a __thiscall */
#ifdef __i386__

#include "pshpack1.h"
struct thiscall_thunk
{
    BYTE pop_eax;    /* popl  %eax (ret addr) */
    BYTE pop_edx;    /* popl  %edx (func) */
    BYTE pop_ecx;    /* popl  %ecx (this) */
    BYTE push_eax;   /* pushl %eax */
    WORD jmp_edx;    /* jmp  *%edx */
};
#include "poppack.h"

static void * (WINAPI *call_thiscall_func1)( void *func, void *this );
static void * (WINAPI *call_thiscall_func2)( void *func, void *this, const void *a );
static void * (WINAPI *call_thiscall_func3)( void *func, void *this, const void *a, const void *b );
static void * (WINAPI *call_thiscall_func4)( void *func, void *this, const void *a, const void *b,
        const void *c );
static void * (WINAPI *call_thiscall_func5)( void *func, void *this, const void *a, const void *b,
        const void *c, const void *d );

static void init_thiscall_thunk(void)
{
    struct thiscall_thunk *thunk = VirtualAlloc( NULL, sizeof(*thunk),
                                                 MEM_COMMIT, PAGE_EXECUTE_READWRITE );
    thunk->pop_eax  = 0x58;   /* popl  %eax */
    thunk->pop_edx  = 0x5a;   /* popl  %edx */
    thunk->pop_ecx  = 0x59;   /* popl  %ecx */
    thunk->push_eax = 0x50;   /* pushl %eax */
    thunk->jmp_edx  = 0xe2ff; /* jmp  *%edx */
    call_thiscall_func1 = (void *)thunk;
    call_thiscall_func2 = (void *)thunk;
    call_thiscall_func3 = (void *)thunk;
    call_thiscall_func4 = (void *)thunk;
    call_thiscall_func5 = (void *)thunk;
}

#define call_func1(func,_this) call_thiscall_func1(func,_this)
#define call_func2(func,_this,a) call_thiscall_func2(func,_this,(const void*)(a))
#define call_func3(func,_this,a,b) call_thiscall_func3(func,_this,(const void*)(a),(const void*)(b))
#define call_func4(func,_this,a,b,c) call_thiscall_func4(func,_this,(const void*)(a),(const void*)(b), \
        (const void*)(c))
#define call_func5(func,_this,a,b,c,d) call_thiscall_func5(func,_this,(const void*)(a),(const void*)(b), \
        (const void*)(c), (const void *)(d))

#else

#define init_thiscall_thunk()
#define call_func1(func,_this) func(_this)
#define call_func2(func,_this,a) func(_this,a)
#define call_func3(func,_this,a,b) func(_this,a,b)
#define call_func4(func,_this,a,b,c) func(_this,a,b,c)
#define call_func5(func,_this,a,b,c,d) func(_this,a,b,c,d)

#endif /* __i386__ */

static HMODULE msvcrt, msvcirt;
#define SETNOFAIL(x,y) x = (void*)GetProcAddress(msvcirt,y)
#define SET(x,y) do { SETNOFAIL(x,y); ok(x != NULL, "Export '%s' not found\n", y); } while(0)
static BOOL init(void)
{
    msvcrt = LoadLibraryA("msvcrt.dll");
    msvcirt = LoadLibraryA("msvcirt.dll");
    if(!msvcirt) {
        win_skip("msvcirt.dll not installed\n");
        return FALSE;
    }

    if(sizeof(void*) == 8) { /* 64-bit initialization */
        p_operator_new = (void*)GetProcAddress(msvcrt, "??2@YAPEAX_K@Z");

        SET(p_streambuf_reserve_ctor, "??0streambuf@@IEAA@PEADH@Z");
        SET(p_streambuf_ctor, "??0streambuf@@IEAA@XZ");
        SET(p_streambuf_dtor, "??1streambuf@@UEAA@XZ");
        SET(p_streambuf_allocate, "?allocate@streambuf@@IEAAHXZ");
        SET(p_streambuf_clrclock, "?clrlock@streambuf@@QEAAXXZ");
        SET(p_streambuf_doallocate, "?doallocate@streambuf@@MEAAHXZ");
        SET(p_streambuf_gbump, "?gbump@streambuf@@IEAAXH@Z");
        SET(p_streambuf_lock, "?lock@streambuf@@QEAAXXZ");
        SET(p_streambuf_pbackfail, "?pbackfail@streambuf@@UEAAHH@Z");
        SET(p_streambuf_pbump, "?pbump@streambuf@@IEAAXH@Z");
        SET(p_streambuf_sbumpc, "?sbumpc@streambuf@@QEAAHXZ");
        SET(p_streambuf_setb, "?setb@streambuf@@IEAAXPEAD0H@Z");
        SET(p_streambuf_setbuf, "?setbuf@streambuf@@UEAAPEAV1@PEADH@Z");
        SET(p_streambuf_setlock, "?setlock@streambuf@@QEAAXXZ");
        SET(p_streambuf_sgetc, "?sgetc@streambuf@@QEAAHXZ");
        SET(p_streambuf_snextc, "?snextc@streambuf@@QEAAHXZ");
        SET(p_streambuf_sputc, "?sputc@streambuf@@QEAAHH@Z");
        SET(p_streambuf_stossc, "?stossc@streambuf@@QEAAXXZ");
        SET(p_streambuf_sync, "?sync@streambuf@@UEAAHXZ");
        SET(p_streambuf_unlock, "?unlock@streambuf@@QEAAXXZ");
        SET(p_streambuf_xsgetn, "?xsgetn@streambuf@@UEAAHPEADH@Z");
        SET(p_streambuf_xsputn, "?xsputn@streambuf@@UEAAHPEBDH@Z");

        SET(p_filebuf_fd_ctor, "??0filebuf@@QEAA@H@Z");
        SET(p_filebuf_fd_reserve_ctor, "??0filebuf@@QEAA@HPEADH@Z");
        SET(p_filebuf_ctor, "??0filebuf@@QEAA@XZ");
        SET(p_filebuf_dtor, "??1filebuf@@UEAA@XZ");
        SET(p_filebuf_attach, "?attach@filebuf@@QEAAPEAV1@H@Z");
        SET(p_filebuf_open, "?open@filebuf@@QEAAPEAV1@PEBDHH@Z");
        SET(p_filebuf_close, "?close@filebuf@@QEAAPEAV1@XZ");
        SET(p_filebuf_setmode, "?setmode@filebuf@@QEAAHH@Z");
        SET(p_filebuf_setbuf, "?setbuf@filebuf@@UEAAPEAVstreambuf@@PEADH@Z");

        SET(p_ios_copy_ctor, "??0ios@@IEAA@AEBV0@@Z");
        SET(p_ios_ctor, "??0ios@@IEAA@XZ");
        SET(p_ios_sb_ctor, "??0ios@@QEAA@PEAVstreambuf@@@Z");
        SET(p_ios_assign, "??4ios@@IEAAAEAV0@AEBV0@@Z");
        SET(p_ios_init, "?init@ios@@IEAAXPEAVstreambuf@@@Z");
        SET(p_ios_dtor, "??1ios@@UEAA@XZ");
        SET(p_ios_clrlock, "?clrlock@ios@@QEAAXXZ");
        SET(p_ios_setlock, "?setlock@ios@@QEAAXXZ");
        SET(p_ios_lock, "?lock@ios@@QEAAXXZ");
        SET(p_ios_unlock, "?unlock@ios@@QEAAXXZ");
        SET(p_ios_lockbuf, "?lockbuf@ios@@QEAAXXZ");
        SET(p_ios_unlockbuf, "?unlockbuf@ios@@QEAAXXZ");
        SET(p_ios_flags_set, "?flags@ios@@QEAAJJ@Z");
        SET(p_ios_flags_get, "?flags@ios@@QEBAJXZ");
        SET(p_ios_setf, "?setf@ios@@QEAAJJ@Z");
        SET(p_ios_setf_mask, "?setf@ios@@QEAAJJJ@Z");
        SET(p_ios_unsetf, "?unsetf@ios@@QEAAJJ@Z");
        SET(p_ios_good, "?good@ios@@QEBAHXZ");
        SET(p_ios_bad, "?bad@ios@@QEBAHXZ");
        SET(p_ios_eof, "?eof@ios@@QEBAHXZ");
        SET(p_ios_fail, "?fail@ios@@QEBAHXZ");
        SET(p_ios_clear, "?clear@ios@@QEAAXH@Z");
        SET(p_ios_iword, "?iword@ios@@QEBAAEAJH@Z");
        SET(p_ios_pword, "?pword@ios@@QEBAAEAPEAXH@Z");
    } else {
        p_operator_new = (void*)GetProcAddress(msvcrt, "??2@YAPAXI@Z");

        SET(p_streambuf_reserve_ctor, "??0streambuf@@IAE@PADH@Z");
        SET(p_streambuf_ctor, "??0streambuf@@IAE@XZ");
        SET(p_streambuf_dtor, "??1streambuf@@UAE@XZ");
        SET(p_streambuf_allocate, "?allocate@streambuf@@IAEHXZ");
        SET(p_streambuf_clrclock, "?clrlock@streambuf@@QAEXXZ");
        SET(p_streambuf_doallocate, "?doallocate@streambuf@@MAEHXZ");
        SET(p_streambuf_gbump, "?gbump@streambuf@@IAEXH@Z");
        SET(p_streambuf_lock, "?lock@streambuf@@QAEXXZ");
        SET(p_streambuf_pbackfail, "?pbackfail@streambuf@@UAEHH@Z");
        SET(p_streambuf_pbump, "?pbump@streambuf@@IAEXH@Z");
        SET(p_streambuf_sbumpc, "?sbumpc@streambuf@@QAEHXZ");
        SET(p_streambuf_setb, "?setb@streambuf@@IAEXPAD0H@Z");
        SET(p_streambuf_setbuf, "?setbuf@streambuf@@UAEPAV1@PADH@Z");
        SET(p_streambuf_setlock, "?setlock@streambuf@@QAEXXZ");
        SET(p_streambuf_sgetc, "?sgetc@streambuf@@QAEHXZ");
        SET(p_streambuf_snextc, "?snextc@streambuf@@QAEHXZ");
        SET(p_streambuf_sputc, "?sputc@streambuf@@QAEHH@Z");
        SET(p_streambuf_stossc, "?stossc@streambuf@@QAEXXZ");
        SET(p_streambuf_sync, "?sync@streambuf@@UAEHXZ");
        SET(p_streambuf_unlock, "?unlock@streambuf@@QAEXXZ");
        SET(p_streambuf_xsgetn, "?xsgetn@streambuf@@UAEHPADH@Z");
        SET(p_streambuf_xsputn, "?xsputn@streambuf@@UAEHPBDH@Z");

        SET(p_filebuf_fd_ctor, "??0filebuf@@QAE@H@Z");
        SET(p_filebuf_fd_reserve_ctor, "??0filebuf@@QAE@HPADH@Z");
        SET(p_filebuf_ctor, "??0filebuf@@QAE@XZ");
        SET(p_filebuf_dtor, "??1filebuf@@UAE@XZ");
        SET(p_filebuf_attach, "?attach@filebuf@@QAEPAV1@H@Z");
        SET(p_filebuf_open, "?open@filebuf@@QAEPAV1@PBDHH@Z");
        SET(p_filebuf_close, "?close@filebuf@@QAEPAV1@XZ");
        SET(p_filebuf_setmode, "?setmode@filebuf@@QAEHH@Z");
        SET(p_filebuf_setbuf, "?setbuf@filebuf@@UAEPAVstreambuf@@PADH@Z");

        SET(p_ios_copy_ctor, "??0ios@@IAE@ABV0@@Z");
        SET(p_ios_ctor, "??0ios@@IAE@XZ");
        SET(p_ios_sb_ctor, "??0ios@@QAE@PAVstreambuf@@@Z");
        SET(p_ios_assign, "??4ios@@IAEAAV0@ABV0@@Z");
        SET(p_ios_init, "?init@ios@@IAEXPAVstreambuf@@@Z");
        SET(p_ios_dtor, "??1ios@@UAE@XZ");
        SET(p_ios_clrlock, "?clrlock@ios@@QAAXXZ");
        SET(p_ios_setlock, "?setlock@ios@@QAAXXZ");
        SET(p_ios_lock, "?lock@ios@@QAAXXZ");
        SET(p_ios_unlock, "?unlock@ios@@QAAXXZ");
        SET(p_ios_lockbuf, "?lockbuf@ios@@QAAXXZ");
        SET(p_ios_unlockbuf, "?unlockbuf@ios@@QAAXXZ");
        SET(p_ios_flags_set, "?flags@ios@@QAEJJ@Z");
        SET(p_ios_flags_get, "?flags@ios@@QBEJXZ");
        SET(p_ios_setf, "?setf@ios@@QAEJJ@Z");
        SET(p_ios_setf_mask, "?setf@ios@@QAEJJJ@Z");
        SET(p_ios_unsetf, "?unsetf@ios@@QAEJJ@Z");
        SET(p_ios_good, "?good@ios@@QBEHXZ");
        SET(p_ios_bad, "?bad@ios@@QBEHXZ");
        SET(p_ios_eof, "?eof@ios@@QBEHXZ");
        SET(p_ios_fail, "?fail@ios@@QBEHXZ");
        SET(p_ios_clear, "?clear@ios@@QAEXH@Z");
        SET(p_ios_iword, "?iword@ios@@QBEAAJH@Z");
        SET(p_ios_pword, "?pword@ios@@QBEAAPAXH@Z");
    }
    SET(p_ios_static_lock, "?x_lockc@ios@@0U_CRT_CRITICAL_SECTION@@A");
    SET(p_ios_lockc, "?lockc@ios@@KAXXZ");
    SET(p_ios_unlockc, "?unlockc@ios@@KAXXZ");
    SET(p_ios_maxbit, "?x_maxbit@ios@@0JA");
    SET(p_ios_bitalloc, "?bitalloc@ios@@SAJXZ");
    SET(p_ios_curindex, "?x_curindex@ios@@0HA");
    SET(p_ios_statebuf, "?x_statebuf@ios@@0PAJA");
    SET(p_ios_xalloc, "?xalloc@ios@@SAHXZ");
    SET(p_ios_fLockcInit, "?fLockcInit@ios@@0HA");

    init_thiscall_thunk();
    return TRUE;
}

static int overflow_count, underflow_count;
static streambuf *test_this;
static char test_get_buffer[24];
static int buffer_pos, get_end;

#ifdef __i386__
static int __thiscall test_streambuf_overflow(int ch)
#else
static int __thiscall test_streambuf_overflow(streambuf *this, int ch)
#endif
{
    overflow_count++;
    if (ch == 'L') /* simulate a failure */
        return EOF;
    if (!test_this->unbuffered)
        test_this->pptr = test_this->pbase + 5;
    return ch;
}

#ifdef __i386__
static int __thiscall test_streambuf_underflow(void)
#else
static int __thiscall test_streambuf_underflow(streambuf *this)
#endif
{
    underflow_count++;
    if (test_this->unbuffered) {
        return (buffer_pos < 23) ? test_get_buffer[buffer_pos++] : EOF;
    } else if (test_this->gptr < test_this->egptr) {
        return *test_this->gptr;
    } else {
        return get_end ? EOF : *(test_this->gptr = test_this->eback);
    }
}

struct streambuf_lock_arg
{
    streambuf *sb;
    HANDLE lock[4];
    HANDLE test[4];
};

static DWORD WINAPI lock_streambuf(void *arg)
{
    struct streambuf_lock_arg *lock_arg = arg;
    call_func1(p_streambuf_lock, lock_arg->sb);
    SetEvent(lock_arg->lock[0]);
    WaitForSingleObject(lock_arg->test[0], INFINITE);
    call_func1(p_streambuf_lock, lock_arg->sb);
    SetEvent(lock_arg->lock[1]);
    WaitForSingleObject(lock_arg->test[1], INFINITE);
    call_func1(p_streambuf_lock, lock_arg->sb);
    SetEvent(lock_arg->lock[2]);
    WaitForSingleObject(lock_arg->test[2], INFINITE);
    call_func1(p_streambuf_unlock, lock_arg->sb);
    SetEvent(lock_arg->lock[3]);
    WaitForSingleObject(lock_arg->test[3], INFINITE);
    call_func1(p_streambuf_unlock, lock_arg->sb);
    return 0;
}

static void test_streambuf(void)
{
    streambuf sb, sb2, sb3, *psb;
    vtable_ptr test_streambuf_vtbl[11];
    struct streambuf_lock_arg lock_arg;
    HANDLE thread;
    char reserve[16];
    int ret, i;
    BOOL locked;

    memset(&sb, 0xab, sizeof(streambuf));
    memset(&sb2, 0xab, sizeof(streambuf));
    memset(&sb3, 0xab, sizeof(streambuf));

    /* constructors */
    call_func1(p_streambuf_ctor, &sb);
    ok(sb.allocated == 0, "wrong allocate value, expected 0 got %d\n", sb.allocated);
    ok(sb.unbuffered == 0, "wrong unbuffered value, expected 0 got %d\n", sb.unbuffered);
    ok(sb.base == NULL, "wrong base pointer, expected %p got %p\n", NULL, sb.base);
    ok(sb.ebuf == NULL, "wrong ebuf pointer, expected %p got %p\n", NULL, sb.ebuf);
    ok(sb.lock.LockCount == -1, "wrong critical section state, expected -1 got %d\n", sb.lock.LockCount);
    call_func3(p_streambuf_reserve_ctor, &sb2, reserve, 16);
    ok(sb2.allocated == 0, "wrong allocate value, expected 0 got %d\n", sb2.allocated);
    ok(sb2.unbuffered == 0, "wrong unbuffered value, expected 0 got %d\n", sb2.unbuffered);
    ok(sb2.base == reserve, "wrong base pointer, expected %p got %p\n", reserve, sb2.base);
    ok(sb2.ebuf == reserve+16, "wrong ebuf pointer, expected %p got %p\n", reserve+16, sb2.ebuf);
    ok(sb.lock.LockCount == -1, "wrong critical section state, expected -1 got %d\n", sb.lock.LockCount);
    call_func1(p_streambuf_ctor, &sb3);
    ok(sb3.allocated == 0, "wrong allocate value, expected 0 got %d\n", sb3.allocated);
    ok(sb3.unbuffered == 0, "wrong unbuffered value, expected 0 got %d\n", sb3.unbuffered);
    ok(sb3.base == NULL, "wrong base pointer, expected %p got %p\n", NULL, sb3.base);
    ok(sb3.ebuf == NULL, "wrong ebuf pointer, expected %p got %p\n", NULL, sb3.ebuf);

    memcpy(test_streambuf_vtbl, sb.vtable, sizeof(test_streambuf_vtbl));
    test_streambuf_vtbl[7] = (vtable_ptr)&test_streambuf_overflow;
    test_streambuf_vtbl[8] = (vtable_ptr)&test_streambuf_underflow;
    sb2.vtable = test_streambuf_vtbl;
    sb3.vtable = test_streambuf_vtbl;
    overflow_count = underflow_count = 0;
    strcpy(test_get_buffer, "CompuGlobalHyperMegaNet");
    buffer_pos = get_end = 0;

    /* setlock */
    ok(sb.do_lock == -1, "expected do_lock value -1, got %d\n", sb.do_lock);
    call_func1(p_streambuf_setlock, &sb);
    ok(sb.do_lock == -2, "expected do_lock value -2, got %d\n", sb.do_lock);
    call_func1(p_streambuf_setlock, &sb);
    ok(sb.do_lock == -3, "expected do_lock value -3, got %d\n", sb.do_lock);
    sb.do_lock = 3;
    call_func1(p_streambuf_setlock, &sb);
    ok(sb.do_lock == 2, "expected do_lock value 2, got %d\n", sb.do_lock);

    /* clrlock */
    sb.do_lock = -2;
    call_func1(p_streambuf_clrclock, &sb);
    ok(sb.do_lock == -1, "expected do_lock value -1, got %d\n", sb.do_lock);
    call_func1(p_streambuf_clrclock, &sb);
    ok(sb.do_lock == 0, "expected do_lock value 0, got %d\n", sb.do_lock);
    call_func1(p_streambuf_clrclock, &sb);
    ok(sb.do_lock == 1, "expected do_lock value 1, got %d\n", sb.do_lock);
    call_func1(p_streambuf_clrclock, &sb);
    ok(sb.do_lock == 1, "expected do_lock value 1, got %d\n", sb.do_lock);

    /* lock/unlock */
    lock_arg.sb = &sb;
    for (i = 0; i < 4; i++) {
        lock_arg.lock[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        ok(lock_arg.lock[i] != NULL, "CreateEventW failed\n");
        lock_arg.test[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        ok(lock_arg.test[i] != NULL, "CreateEventW failed\n");
    }

    sb.do_lock = 0;
    thread = CreateThread(NULL, 0, lock_streambuf, (void*)&lock_arg, 0, NULL);
    ok(thread != NULL, "CreateThread failed\n");
    WaitForSingleObject(lock_arg.lock[0], INFINITE);
    locked = TryEnterCriticalSection(&sb.lock);
    ok(locked != 0, "could not lock the streambuf\n");
    LeaveCriticalSection(&sb.lock);

    sb.do_lock = 1;
    SetEvent(lock_arg.test[0]);
    WaitForSingleObject(lock_arg.lock[1], INFINITE);
    locked = TryEnterCriticalSection(&sb.lock);
    ok(locked != 0, "could not lock the streambuf\n");
    LeaveCriticalSection(&sb.lock);

    sb.do_lock = -1;
    SetEvent(lock_arg.test[1]);
    WaitForSingleObject(lock_arg.lock[2], INFINITE);
    locked = TryEnterCriticalSection(&sb.lock);
    ok(locked == 0, "the streambuf was not locked before\n");

    sb.do_lock = 0;
    SetEvent(lock_arg.test[2]);
    WaitForSingleObject(lock_arg.lock[3], INFINITE);
    locked = TryEnterCriticalSection(&sb.lock);
    ok(locked == 0, "the streambuf was not locked before\n");
    sb.do_lock = -1;

    /* setb */
    call_func4(p_streambuf_setb, &sb, reserve, reserve+16, 0);
    ok(sb.base == reserve, "wrong base pointer, expected %p got %p\n", reserve, sb.base);
    ok(sb.ebuf == reserve+16, "wrong ebuf pointer, expected %p got %p\n", reserve+16, sb.ebuf);
    call_func4(p_streambuf_setb, &sb, reserve, reserve+16, 4);
    ok(sb.allocated == 4, "wrong allocate value, expected 4 got %d\n", sb.allocated);
    sb.allocated = 0;
    call_func4(p_streambuf_setb, &sb, NULL, NULL, 3);
    ok(sb.allocated == 3, "wrong allocate value, expected 3 got %d\n", sb.allocated);

    /* setbuf */
    psb = call_func3(p_streambuf_setbuf, &sb, NULL, 5);
    ok(psb == &sb, "wrong return value, expected %p got %p\n", &sb, psb);
    ok(sb.allocated == 3, "wrong allocate value, expected 3 got %d\n", sb.allocated);
    ok(sb.unbuffered == 1, "wrong unbuffered value, expected 1 got %d\n", sb.unbuffered);
    ok(sb.base == NULL, "wrong base pointer, expected %p got %p\n", NULL, sb.base);
    ok(sb.ebuf == NULL, "wrong ebuf pointer, expected %p got %p\n", NULL, sb.ebuf);
    psb = call_func3(p_streambuf_setbuf, &sb, reserve, 0);
    ok(psb == &sb, "wrong return value, expected %p got %p\n", &sb, psb);
    ok(sb.unbuffered == 1, "wrong unbuffered value, expected 1 got %d\n", sb.unbuffered);
    ok(sb.base == NULL, "wrong base pointer, expected %p got %p\n", NULL, sb.base);
    ok(sb.ebuf == NULL, "wrong ebuf pointer, expected %p got %p\n", NULL, sb.ebuf);
    psb = call_func3(p_streambuf_setbuf, &sb, reserve, 16);
    ok(psb == &sb, "wrong return value, expected %p got %p\n", &sb, psb);
    ok(sb.allocated == 3, "wrong allocate value, expected 3 got %d\n", sb.allocated);
    ok(sb.unbuffered == 0, "wrong unbuffered value, expected 0 got %d\n", sb.unbuffered);
    ok(sb.base == reserve, "wrong base pointer, expected %p got %p\n", reserve, sb.base);
    ok(sb.ebuf == reserve+16, "wrong ebuf pointer, expected %p got %p\n", reserve+16, sb.ebuf);
    psb = call_func3(p_streambuf_setbuf, &sb, NULL, 8);
    ok(psb == NULL, "wrong return value, expected %p got %p\n", NULL, psb);
    ok(sb.unbuffered == 0, "wrong unbuffered value, expected 0 got %d\n", sb.unbuffered);
    ok(sb.base == reserve, "wrong base pointer, expected %p got %p\n", reserve, sb.base);
    ok(sb.ebuf == reserve+16, "wrong ebuf pointer, expected %p got %p\n", reserve+16, sb.ebuf);

    /* allocate */
    ret = (int) call_func1(p_streambuf_allocate, &sb);
    ok(ret == 0, "wrong return value, expected 0 got %d\n", ret);
    sb.base = NULL;
    ret = (int) call_func1(p_streambuf_allocate, &sb);
    ok(ret == 1, "wrong return value, expected 1 got %d\n", ret);
    ok(sb.allocated == 1, "wrong allocate value, expected 1 got %d\n", sb.allocated);
    ok(sb.ebuf - sb.base == 512 , "wrong reserve area size, expected 512 got %p-%p\n", sb.ebuf, sb.base);

    /* doallocate */
    ret = (int) call_func1(p_streambuf_doallocate, &sb2);
    ok(ret == 1, "doallocate failed, got %d\n", ret);
    ok(sb2.allocated == 1, "wrong allocate value, expected 1 got %d\n", sb2.allocated);
    ok(sb2.ebuf - sb2.base == 512 , "wrong reserve area size, expected 512 got %p-%p\n", sb2.ebuf, sb2.base);
    ret = (int) call_func1(p_streambuf_doallocate, &sb3);
    ok(ret == 1, "doallocate failed, got %d\n", ret);
    ok(sb3.allocated == 1, "wrong allocate value, expected 1 got %d\n", sb3.allocated);
    ok(sb3.ebuf - sb3.base == 512 , "wrong reserve area size, expected 512 got %p-%p\n", sb3.ebuf, sb3.base);

    /* sb: buffered, space available */
    sb.eback = sb.gptr = sb.base;
    sb.egptr = sb.base + 256;
    sb.pbase = sb.pptr = sb.base + 256;
    sb.epptr = sb.base + 512;
    /* sb2: buffered, no space available */
    sb2.eback = sb2.base;
    sb2.gptr = sb2.egptr = sb2.base + 256;
    sb2.pbase = sb2.base + 256;
    sb2.pptr = sb2.epptr = sb2.base + 512;
    /* sb3: unbuffered */
    sb3.unbuffered = 1;

    /* gbump */
    call_func2(p_streambuf_gbump, &sb, 10);
    ok(sb.gptr == sb.eback + 10, "advance get pointer failed, expected %p got %p\n", sb.eback + 10, sb.gptr);
    call_func2(p_streambuf_gbump, &sb, -15);
    ok(sb.gptr == sb.eback - 5, "advance get pointer failed, expected %p got %p\n", sb.eback - 5, sb.gptr);
    sb.gptr = sb.eback;

    /* pbump */
    call_func2(p_streambuf_pbump, &sb, -2);
    ok(sb.pptr == sb.pbase - 2, "advance put pointer failed, expected %p got %p\n", sb.pbase - 2, sb.pptr);
    call_func2(p_streambuf_pbump, &sb, 20);
    ok(sb.pptr == sb.pbase + 18, "advance put pointer failed, expected %p got %p\n", sb.pbase + 18, sb.pptr);
    sb.pptr = sb.pbase;

    /* sync */
    ret = (int) call_func1(p_streambuf_sync, &sb);
    ok(ret == EOF, "sync failed, expected EOF got %d\n", ret);
    sb.gptr = sb.egptr;
    ret = (int) call_func1(p_streambuf_sync, &sb);
    ok(ret == 0, "sync failed, expected 0 got %d\n", ret);
    sb.gptr = sb.egptr + 1;
    ret = (int) call_func1(p_streambuf_sync, &sb);
    ok(ret == 0, "sync failed, expected 0 got %d\n", ret);
    sb.gptr = sb.eback;
    ret = (int) call_func1(p_streambuf_sync, &sb2);
    ok(ret == EOF, "sync failed, expected EOF got %d\n", ret);
    sb2.pptr = sb2.pbase;
    ret = (int) call_func1(p_streambuf_sync, &sb2);
    ok(ret == 0, "sync failed, expected 0 got %d\n", ret);
    sb2.pptr = sb2.pbase - 1;
    ret = (int) call_func1(p_streambuf_sync, &sb2);
    ok(ret == 0, "sync failed, expected 0 got %d\n", ret);
    sb2.pptr = sb2.epptr;
    ret = (int) call_func1(p_streambuf_sync, &sb3);
    ok(ret == 0, "sync failed, expected 0 got %d\n", ret);

    /* sgetc */
    strcpy(sb2.eback, "WorstTestEver");
    test_this = &sb2;
    ret = (int) call_func1(p_streambuf_sgetc, &sb2);
    ok(ret == 'W', "expected 'W' got '%c'\n", ret);
    ok(underflow_count == 1, "expected call to underflow\n");
    ok(sb2.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb2.stored_char);
    sb2.gptr++;
    ret = (int) call_func1(p_streambuf_sgetc, &sb2);
    ok(ret == 'o', "expected 'o' got '%c'\n", ret);
    ok(underflow_count == 2, "expected call to underflow\n");
    ok(sb2.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb2.stored_char);
    sb2.gptr = sb2.egptr;
    test_this = &sb3;
    ret = (int) call_func1(p_streambuf_sgetc, &sb3);
    ok(ret == 'C', "expected 'C' got '%c'\n", ret);
    ok(underflow_count == 3, "expected call to underflow\n");
    ok(sb3.stored_char == 'C', "wrong stored character, expected 'C' got %c\n", sb3.stored_char);
    sb3.stored_char = 'b';
    ret = (int) call_func1(p_streambuf_sgetc, &sb3);
    ok(ret == 'b', "expected 'b' got '%c'\n", ret);
    ok(underflow_count == 3, "no call to underflow expected\n");
    ok(sb3.stored_char == 'b', "wrong stored character, expected 'b' got %c\n", sb3.stored_char);

    /* sputc */
    *sb.pbase = 'a';
    ret = (int) call_func2(p_streambuf_sputc, &sb, 'c');
    ok(ret == 'c', "wrong return value, expected 'c' got %d\n", ret);
    ok(overflow_count == 0, "no call to overflow expected\n");
    ok(*sb.pbase == 'c', "expected 'c' in the put area, got %c\n", *sb.pbase);
    ok(sb.pptr == sb.pbase + 1, "wrong put pointer, expected %p got %p\n", sb.pbase + 1, sb.pptr);
    test_this = &sb2;
    ret = (int) call_func2(p_streambuf_sputc, &sb2, 'c');
    ok(ret == 'c', "wrong return value, expected 'c' got %d\n", ret);
    ok(overflow_count == 1, "expected call to overflow\n");
    ok(sb2.pptr == sb2.pbase + 5, "wrong put pointer, expected %p got %p\n", sb2.pbase + 5, sb2.pptr);
    test_this = &sb3;
    ret = (int) call_func2(p_streambuf_sputc, &sb3, 'c');
    ok(ret == 'c', "wrong return value, expected 'c' got %d\n", ret);
    ok(overflow_count == 2, "expected call to overflow\n");
    sb3.pbase = sb3.pptr = sb3.base;
    sb3.epptr = sb3.ebuf;
    ret = (int) call_func2(p_streambuf_sputc, &sb3, 'c');
    ok(ret == 'c', "wrong return value, expected 'c' got %d\n", ret);
    ok(overflow_count == 2, "no call to overflow expected\n");
    ok(*sb3.pbase == 'c', "expected 'c' in the put area, got %c\n", *sb3.pbase);
    sb3.pbase = sb3.pptr = sb3.epptr = NULL;

    /* xsgetn */
    sb2.gptr = sb2.egptr = sb2.eback + 13;
    test_this = &sb2;
    ret = (int) call_func3(p_streambuf_xsgetn, &sb2, reserve, 5);
    ok(ret == 5, "wrong return value, expected 5 got %d\n", ret);
    ok(!strncmp(reserve, "Worst", 5), "expected 'Worst' got %s\n", reserve);
    ok(sb2.gptr == sb2.eback + 5, "wrong get pointer, expected %p got %p\n", sb2.eback + 5, sb2.gptr);
    ok(underflow_count == 4, "expected call to underflow\n");
    ret = (int) call_func3(p_streambuf_xsgetn, &sb2, reserve, 4);
    ok(ret == 4, "wrong return value, expected 4 got %d\n", ret);
    ok(!strncmp(reserve, "Test", 4), "expected 'Test' got %s\n", reserve);
    ok(sb2.gptr == sb2.eback + 9, "wrong get pointer, expected %p got %p\n", sb2.eback + 9, sb2.gptr);
    ok(underflow_count == 5, "expected call to underflow\n");
    get_end = 1;
    ret = (int) call_func3(p_streambuf_xsgetn, &sb2, reserve, 16);
    ok(ret == 4, "wrong return value, expected 4 got %d\n", ret);
    ok(!strncmp(reserve, "Ever", 4), "expected 'Ever' got %s\n", reserve);
    ok(sb2.gptr == sb2.egptr, "wrong get pointer, expected %p got %p\n", sb2.egptr, sb2.gptr);
    ok(underflow_count == 7, "expected 2 calls to underflow, got %d\n", underflow_count - 5);
    test_this = &sb3;
    ret = (int) call_func3(p_streambuf_xsgetn, &sb3, reserve, 11);
    ok(ret == 11, "wrong return value, expected 11 got %d\n", ret);
    ok(!strncmp(reserve, "bompuGlobal", 11), "expected 'bompuGlobal' got %s\n", reserve);
    ok(sb3.stored_char == 'H', "wrong stored character, expected 'H' got %c\n", sb3.stored_char);
    ok(underflow_count == 18, "expected 11 calls to underflow, got %d\n", underflow_count - 7);
    ret = (int) call_func3(p_streambuf_xsgetn, &sb3, reserve, 16);
    ok(ret == 12, "wrong return value, expected 12 got %d\n", ret);
    ok(!strncmp(reserve, "HyperMegaNet", 12), "expected 'HyperMegaNet' got %s\n", reserve);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 30, "expected 12 calls to underflow, got %d\n", underflow_count - 18);
    ret = (int) call_func3(p_streambuf_xsgetn, &sb3, reserve, 3);
    ok(ret == 0, "wrong return value, expected 0 got %d\n", ret);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 31, "expected call to underflow\n");
    buffer_pos = 0;
    ret = (int) call_func3(p_streambuf_xsgetn, &sb3, reserve, 5);
    ok(ret == 5, "wrong return value, expected 5 got %d\n", ret);
    ok(!strncmp(reserve, "Compu", 5), "expected 'Compu' got %s\n", reserve);
    ok(sb3.stored_char == 'G', "wrong stored character, expected 'G' got %c\n", sb3.stored_char);
    ok(underflow_count == 37, "expected 6 calls to underflow, got %d\n", underflow_count - 31);

    /* xsputn */
    ret = (int) call_func3(p_streambuf_xsputn, &sb, "Test\0ing", 8);
    ok(ret == 8, "wrong return value, expected 8 got %d\n", ret);
    ok(sb.pptr == sb.pbase + 9, "wrong put pointer, expected %p got %p\n", sb.pbase + 9, sb.pptr);
    test_this = &sb2;
    sb2.pptr = sb2.epptr - 7;
    ret = (int) call_func3(p_streambuf_xsputn, &sb2, "Testing", 7);
    ok(ret == 7, "wrong return value, expected 7 got %d\n", ret);
    ok(sb2.pptr == sb2.epptr, "wrong put pointer, expected %p got %p\n", sb2.epptr, sb2.pptr);
    ok(overflow_count == 2, "no call to overflow expected\n");
    sb2.pptr = sb2.epptr - 5;
    sb2.pbase[5] = 'a';
    ret = (int) call_func3(p_streambuf_xsputn, &sb2, "Testing", 7);
    ok(ret == 7, "wrong return value, expected 7 got %d\n", ret);
    ok(sb2.pbase[5] == 'g', "expected 'g' got %c\n", sb2.pbase[5]);
    ok(sb2.pptr == sb2.pbase + 6, "wrong put pointer, expected %p got %p\n", sb2.pbase + 6, sb2.pptr);
    ok(overflow_count == 3, "expected call to overflow\n");
    sb2.pptr = sb2.epptr - 4;
    ret = (int) call_func3(p_streambuf_xsputn, &sb2, "TestLing", 8);
    ok(ret == 4, "wrong return value, expected 4 got %d\n", ret);
    ok(sb2.pptr == sb2.epptr, "wrong put pointer, expected %p got %p\n", sb2.epptr, sb2.pptr);
    ok(overflow_count == 4, "expected call to overflow\n");
    test_this = &sb3;
    ret = (int) call_func3(p_streambuf_xsputn, &sb3, "Testing", 7);
    ok(ret == 7, "wrong return value, expected 7 got %d\n", ret);
    ok(sb3.stored_char == 'G', "wrong stored character, expected 'G' got %c\n", sb3.stored_char);
    ok(overflow_count == 11, "expected 7 calls to overflow, got %d\n", overflow_count - 4);
    ret = (int) call_func3(p_streambuf_xsputn, &sb3, "TeLephone", 9);
    ok(ret == 2, "wrong return value, expected 2 got %d\n", ret);
    ok(sb3.stored_char == 'G', "wrong stored character, expected 'G' got %c\n", sb3.stored_char);
    ok(overflow_count == 14, "expected 3 calls to overflow, got %d\n", overflow_count - 11);

    /* snextc */
    strcpy(sb.eback, "Test");
    ret = (int) call_func1(p_streambuf_snextc, &sb);
    ok(ret == 'e', "expected 'e' got '%c'\n", ret);
    ok(sb.gptr == sb.eback + 1, "wrong get pointer, expected %p got %p\n", sb.eback + 1, sb.gptr);
    test_this = &sb2;
    ret = (int) call_func1(p_streambuf_snextc, &sb2);
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb2.gptr == sb2.egptr + 1, "wrong get pointer, expected %p got %p\n", sb2.egptr + 1, sb2.gptr);
    ok(underflow_count == 39, "expected 2 calls to underflow, got %d\n", underflow_count - 37);
    sb2.gptr = sb2.egptr - 1;
    ret = (int) call_func1(p_streambuf_snextc, &sb2);
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb2.gptr == sb2.egptr, "wrong get pointer, expected %p got %p\n", sb2.egptr, sb2.gptr);
    ok(underflow_count == 40, "expected call to underflow\n");
    get_end = 0;
    ret = (int) call_func1(p_streambuf_snextc, &sb2);
    ok(ret == 'o', "expected 'o' got '%c'\n", ret);
    ok(sb2.gptr == sb2.eback + 1, "wrong get pointer, expected %p got %p\n", sb2.eback + 1, sb2.gptr);
    ok(underflow_count == 41, "expected call to underflow\n");
    sb2.gptr = sb2.egptr - 1;
    ret = (int) call_func1(p_streambuf_snextc, &sb2);
    ok(ret == 'W', "expected 'W' got '%c'\n", ret);
    ok(sb2.gptr == sb2.eback, "wrong get pointer, expected %p got %p\n", sb2.eback, sb2.gptr);
    ok(underflow_count == 42, "expected call to underflow\n");
    sb2.gptr = sb2.egptr;
    test_this = &sb3;
    ret = (int) call_func1(p_streambuf_snextc, &sb3);
    ok(ret == 'l', "expected 'l' got '%c'\n", ret);
    ok(sb3.stored_char == 'l', "wrong stored character, expected 'l' got %c\n", sb3.stored_char);
    ok(underflow_count == 43, "expected call to underflow\n");
    buffer_pos = 22;
    ret = (int) call_func1(p_streambuf_snextc, &sb3);
    ok(ret == 't', "expected 't' got '%c'\n", ret);
    ok(sb3.stored_char == 't', "wrong stored character, expected 't' got %c\n", sb3.stored_char);
    ok(underflow_count == 44, "expected call to underflow\n");
    ret = (int) call_func1(p_streambuf_snextc, &sb3);
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 45, "expected call to underflow\n");
    buffer_pos = 0;
    ret = (int) call_func1(p_streambuf_snextc, &sb3);
    ok(ret == 'o', "expected 'o' got '%c'\n", ret);
    ok(sb3.stored_char == 'o', "wrong stored character, expected 'o' got %c\n", sb3.stored_char);
    ok(underflow_count == 47, "expected 2 calls to underflow, got %d\n", underflow_count - 45);
    sb3.stored_char = EOF;
    ret = (int) call_func1(p_streambuf_snextc, &sb3);
    ok(ret == 'p', "expected 'p' got '%c'\n", ret);
    ok(sb3.stored_char == 'p', "wrong stored character, expected 'p' got %c\n", sb3.stored_char);
    ok(underflow_count == 49, "expected 2 calls to underflow, got %d\n", underflow_count - 47);

    /* sbumpc */
    ret = (int) call_func1(p_streambuf_sbumpc, &sb);
    ok(ret == 'e', "expected 'e' got '%c'\n", ret);
    ok(sb.gptr == sb.eback + 2, "wrong get pointer, expected %p got %p\n", sb.eback + 2, sb.gptr);
    test_this = &sb2;
    ret = (int) call_func1(p_streambuf_sbumpc, &sb2);
    ok(ret == 'W', "expected 'W' got '%c'\n", ret);
    ok(sb2.gptr == sb2.eback + 1, "wrong get pointer, expected %p got %p\n", sb2.eback + 1, sb2.gptr);
    ok(underflow_count == 50, "expected call to underflow\n");
    sb2.gptr = sb2.egptr - 1;
    *sb2.gptr = 't';
    ret = (int) call_func1(p_streambuf_sbumpc, &sb2);
    ok(ret == 't', "expected 't' got '%c'\n", ret);
    ok(sb2.gptr == sb2.egptr, "wrong get pointer, expected %p got %p\n", sb2.egptr, sb2.gptr);
    ok(underflow_count == 50, "no call to underflow expected\n");
    get_end = 1;
    ret = (int) call_func1(p_streambuf_sbumpc, &sb2);
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb2.gptr == sb2.egptr + 1, "wrong get pointer, expected %p got %p\n", sb2.egptr + 1, sb2.gptr);
    ok(underflow_count == 51, "expected call to underflow\n");
    sb2.gptr = sb2.egptr;
    test_this = &sb3;
    ret = (int) call_func1(p_streambuf_sbumpc, &sb3);
    ok(ret == 'p', "expected 'p' got '%c'\n", ret);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 51, "no call to underflow expected\n");
    ret = (int) call_func1(p_streambuf_sbumpc, &sb3);
    ok(ret == 'u', "expected 'u' got '%c'\n", ret);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 52, "expected call to underflow\n");
    buffer_pos = 23;
    ret = (int) call_func1(p_streambuf_sbumpc, &sb3);
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 53, "expected call to underflow\n");
    buffer_pos = 0;
    ret = (int) call_func1(p_streambuf_sbumpc, &sb3);
    ok(ret == 'C', "expected 'C' got '%c'\n", ret);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 54, "expected call to underflow\n");

    /* stossc */
    call_func1(p_streambuf_stossc, &sb);
    ok(sb.gptr == sb.eback + 3, "wrong get pointer, expected %p got %p\n", sb.eback + 3, sb.gptr);
    test_this = &sb2;
    call_func1(p_streambuf_stossc, &sb2);
    ok(sb2.gptr == sb2.egptr, "wrong get pointer, expected %p got %p\n", sb2.egptr, sb2.gptr);
    ok(underflow_count == 55, "expected call to underflow\n");
    get_end = 0;
    call_func1(p_streambuf_stossc, &sb2);
    ok(sb2.gptr == sb2.eback + 1, "wrong get pointer, expected %p got %p\n", sb2.eback + 1, sb2.gptr);
    ok(underflow_count == 56, "expected call to underflow\n");
    sb2.gptr = sb2.egptr - 1;
    call_func1(p_streambuf_stossc, &sb2);
    ok(sb2.gptr == sb2.egptr, "wrong get pointer, expected %p got %p\n", sb2.egptr, sb2.gptr);
    ok(underflow_count == 56, "no call to underflow expected\n");
    test_this = &sb3;
    call_func1(p_streambuf_stossc, &sb3);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 57, "expected call to underflow\n");
    sb3.stored_char = 'a';
    call_func1(p_streambuf_stossc, &sb3);
    ok(sb3.stored_char == EOF, "wrong stored character, expected EOF got %c\n", sb3.stored_char);
    ok(underflow_count == 57, "no call to underflow expected\n");

    /* pbackfail */
    ret = (int) call_func2(p_streambuf_pbackfail, &sb, 'A');
    ok(ret == 'A', "expected 'A' got '%c'\n", ret);
    ok(sb.gptr == sb.eback + 2, "wrong get pointer, expected %p got %p\n", sb.eback + 2, sb.gptr);
    ok(*sb.gptr == 'A', "expected 'A' in the get area, got %c\n", *sb.gptr);
    ret = (int) call_func2(p_streambuf_pbackfail, &sb, EOF);
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb.gptr == sb.eback + 1, "wrong get pointer, expected %p got %p\n", sb.eback + 1, sb.gptr);
    ok((signed char)*sb.gptr == EOF, "expected EOF in the get area, got %c\n", *sb.gptr);
    sb.gptr = sb.eback;
    ret = (int) call_func2(p_streambuf_pbackfail, &sb, 'Z');
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb.gptr == sb.eback, "wrong get pointer, expected %p got %p\n", sb.eback, sb.gptr);
    ok(*sb.gptr == 'T', "expected 'T' in the get area, got %c\n", *sb.gptr);
    ret = (int) call_func2(p_streambuf_pbackfail, &sb, EOF);
    ok(ret == EOF, "expected EOF got '%c'\n", ret);
    ok(sb.gptr == sb.eback, "wrong get pointer, expected %p got %p\n", sb.eback, sb.gptr);
    ok(*sb.gptr == 'T', "expected 'T' in the get area, got %c\n", *sb.gptr);
    sb2.gptr = sb2.egptr + 1;
    ret = (int) call_func2(p_streambuf_pbackfail, &sb2, 'X');
    ok(ret == 'X', "expected 'X' got '%c'\n", ret);
    ok(sb2.gptr == sb2.egptr, "wrong get pointer, expected %p got %p\n", sb2.egptr, sb2.gptr);
    ok(*sb2.gptr == 'X', "expected 'X' in the get area, got %c\n", *sb2.gptr);

    SetEvent(lock_arg.test[3]);
    WaitForSingleObject(thread, INFINITE);

    call_func1(p_streambuf_dtor, &sb);
    call_func1(p_streambuf_dtor, &sb2);
    call_func1(p_streambuf_dtor, &sb3);
    for (i = 0; i < 4; i++) {
        CloseHandle(lock_arg.lock[i]);
        CloseHandle(lock_arg.test[i]);
    }
    CloseHandle(thread);
}

struct filebuf_lock_arg
{
    filebuf *fb1, *fb2, *fb3;
    HANDLE lock;
    HANDLE test;
};

static DWORD WINAPI lock_filebuf(void *arg)
{
    struct filebuf_lock_arg *lock_arg = arg;
    call_func1(p_streambuf_lock, &lock_arg->fb1->base);
    call_func1(p_streambuf_lock, &lock_arg->fb2->base);
    call_func1(p_streambuf_lock, &lock_arg->fb3->base);
    SetEvent(lock_arg->lock);
    WaitForSingleObject(lock_arg->test, INFINITE);
    call_func1(p_streambuf_unlock, &lock_arg->fb1->base);
    call_func1(p_streambuf_unlock, &lock_arg->fb2->base);
    call_func1(p_streambuf_unlock, &lock_arg->fb3->base);
    return 0;
}

static void test_filebuf(void)
{
    filebuf fb1, fb2, fb3, *pret;
    struct filebuf_lock_arg lock_arg;
    HANDLE thread;
    const char filename1[] = "test1";
    const char filename2[] = "test2";
    const char filename3[] = "test3";
    char read_buffer[16];
    int ret;

    memset(&fb1, 0xab, sizeof(filebuf));
    memset(&fb2, 0xab, sizeof(filebuf));
    memset(&fb3, 0xab, sizeof(filebuf));

    /* constructors */
    call_func2(p_filebuf_fd_ctor, &fb1, 1);
    ok(fb1.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb1.base.allocated);
    ok(fb1.base.unbuffered == 0, "wrong unbuffered value, expected 0 got %d\n", fb1.base.unbuffered);
    ok(fb1.fd == 1, "wrong fd, expected 1 got %d\n", fb1.fd);
    ok(fb1.close == 0, "wrong value, expected 0 got %d\n", fb1.close);
    call_func4(p_filebuf_fd_reserve_ctor, &fb2, 4, NULL, 0);
    ok(fb2.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb2.base.allocated);
    ok(fb2.base.unbuffered == 1, "wrong unbuffered value, expected 1 got %d\n", fb2.base.unbuffered);
    ok(fb2.fd == 4, "wrong fd, expected 4 got %d\n", fb2.fd);
    ok(fb2.close == 0, "wrong value, expected 0 got %d\n", fb2.close);
    call_func1(p_filebuf_ctor, &fb3);
    ok(fb3.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb3.base.allocated);
    ok(fb3.base.unbuffered == 0, "wrong unbuffered value, expected 0 got %d\n", fb3.base.unbuffered);
    ok(fb3.fd == -1, "wrong fd, expected -1 got %d\n", fb3.fd);
    ok(fb3.close == 0, "wrong value, expected 0 got %d\n", fb3.close);

    lock_arg.fb1 = &fb1;
    lock_arg.fb2 = &fb2;
    lock_arg.fb3 = &fb3;
    lock_arg.lock = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(lock_arg.lock != NULL, "CreateEventW failed\n");
    lock_arg.test = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(lock_arg.test != NULL, "CreateEventW failed\n");
    thread = CreateThread(NULL, 0, lock_filebuf, (void*)&lock_arg, 0, NULL);
    ok(thread != NULL, "CreateThread failed\n");
    WaitForSingleObject(lock_arg.lock, INFINITE);

    /* setbuf */
    fb1.base.do_lock = 0;
    pret = (filebuf*) call_func3(p_filebuf_setbuf, &fb1, read_buffer, 16);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(fb1.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb1.base.allocated);
    ok(fb1.base.base == read_buffer, "wrong buffer, expected %p got %p\n", read_buffer, fb1.base.base);
    ok(fb1.base.pbase == NULL, "wrong put area, expected %p got %p\n", NULL, fb1.base.pbase);
    fb1.base.pbase = fb1.base.pptr = fb1.base.base;
    fb1.base.epptr = fb1.base.ebuf;
    fb1.base.do_lock = -1;
    pret = (filebuf*) call_func3(p_filebuf_setbuf, &fb1, read_buffer, 16);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    ok(fb1.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb1.base.allocated);
    ok(fb1.base.base == read_buffer, "wrong buffer, expected %p got %p\n", read_buffer, fb1.base.base);
    ok(fb1.base.pbase == read_buffer, "wrong put area, expected %p got %p\n", read_buffer, fb1.base.pbase);
    fb1.base.base = fb1.base.ebuf = NULL;
    fb1.base.do_lock = 0;
    pret = (filebuf*) call_func3(p_filebuf_setbuf, &fb1, read_buffer, 0);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(fb1.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb1.base.allocated);
    ok(fb1.base.unbuffered == 1, "wrong unbuffered value, expected 1 got %d\n", fb1.base.unbuffered);
    ok(fb1.base.base == NULL, "wrong buffer, expected %p got %p\n", NULL, fb1.base.base);
    ok(fb1.base.pbase == read_buffer, "wrong put area, expected %p got %p\n", read_buffer, fb1.base.pbase);
    fb1.base.pbase = fb1.base.pptr = fb1.base.epptr = NULL;
    fb1.base.unbuffered = 0;
    fb1.base.do_lock = -1;

    /* attach */
    pret = (filebuf*) call_func2(p_filebuf_attach, &fb1, 2);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    ok(fb1.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb1.base.allocated);
    ok(fb1.fd == 1, "wrong fd, expected 1 got %d\n", fb1.fd);
    fb2.fd = -1;
    fb2.base.do_lock = 0;
    pret = (filebuf*) call_func2(p_filebuf_attach, &fb2, 3);
    ok(pret == &fb2, "wrong return, expected %p got %p\n", &fb2, pret);
    ok(fb2.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb2.base.allocated);
    ok(fb2.fd == 3, "wrong fd, expected 3 got %d\n", fb2.fd);
    fb2.base.do_lock = -1;
    fb3.base.do_lock = 0;
    pret = (filebuf*) call_func2(p_filebuf_attach, &fb3, 2);
    ok(pret == &fb3, "wrong return, expected %p got %p\n", &fb3, pret);
    ok(fb3.base.allocated == 1, "wrong allocate value, expected 1 got %d\n", fb3.base.allocated);
    ok(fb3.fd == 2, "wrong fd, expected 2 got %d\n", fb3.fd);
    fb3.base.do_lock = -1;

    /* open modes */
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_out, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    fb1.fd = -1;
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1,
        OPENMODE_ate|OPENMODE_nocreate|OPENMODE_noreplace|OPENMODE_binary, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    ok(fb1.base.allocated == 0, "wrong allocate value, expected 0 got %d\n", fb1.base.allocated);
    fb1.base.do_lock = 0;
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_out, filebuf_openprot);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(fb1.base.allocated == 1, "wrong allocate value, expected 1 got %d\n", fb1.base.allocated);
    ok(_write(fb1.fd, "testing", 7) == 7, "_write failed\n");
    pret = (filebuf*) call_func1(p_filebuf_close, &fb1);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(fb1.fd == -1, "wrong fd, expected -1 got %d\n", fb1.fd);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_out, filebuf_openprot);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(_read(fb1.fd, read_buffer, 1) == -1, "file should not be open for reading\n");
    pret = (filebuf*) call_func1(p_filebuf_close, &fb1);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_app, filebuf_openprot);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(_read(fb1.fd, read_buffer, 1) == -1, "file should not be open for reading\n");
    ok(_write(fb1.fd, "testing", 7) == 7, "_write failed\n");
    ok(_lseek(fb1.fd, 0, SEEK_SET) == 0, "_lseek failed\n");
    ok(_write(fb1.fd, "append", 6) == 6, "_write failed\n");
    pret = (filebuf*) call_func1(p_filebuf_close, &fb1);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_out|OPENMODE_ate, filebuf_openprot);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(_read(fb1.fd, read_buffer, 1) == -1, "file should not be open for reading\n");
    ok(_lseek(fb1.fd, 0, SEEK_SET) == 0, "_lseek failed\n");
    ok(_write(fb1.fd, "ate", 3) == 3, "_write failed\n");
    pret = (filebuf*) call_func1(p_filebuf_close, &fb1);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_in, filebuf_openprot);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(_read(fb1.fd, read_buffer, 13) == 13, "read failed\n");
    read_buffer[13] = 0;
    ok(!strncmp(read_buffer, "atetingappend", 13), "wrong contents, expected 'atetingappend' got '%s'\n", read_buffer);
    pret = (filebuf*) call_func1(p_filebuf_close, &fb1);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_in|OPENMODE_trunc, filebuf_openprot);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(_read(fb1.fd, read_buffer, 1) == 0, "read failed\n");
    ok(_write(fb1.fd, "file1", 5) == 5, "_write failed\n");
    pret = (filebuf*) call_func1(p_filebuf_close, &fb1);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb1, filename1, OPENMODE_in|OPENMODE_app, filebuf_openprot);
    ok(pret == &fb1, "wrong return, expected %p got %p\n", &fb1, pret);
    ok(_write(fb1.fd, "app", 3) == 3, "_write failed\n");
    ok(_read(fb1.fd, read_buffer, 1) == 0, "read failed\n");
    ok(_lseek(fb1.fd, 0, SEEK_SET) == 0, "_lseek failed\n");
    ok(_read(fb1.fd, read_buffer, 8) == 8, "read failed\n");
    read_buffer[8] = 0;
    ok(!strncmp(read_buffer, "file1app", 8), "wrong contents, expected 'file1app' got '%s'\n", read_buffer);
    fb1.base.do_lock = -1;

    fb2.fd = -1;
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename2, OPENMODE_out|OPENMODE_nocreate, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename2, OPENMODE_in|OPENMODE_nocreate, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    fb2.base.do_lock = 0;
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename2, OPENMODE_in, filebuf_openprot);
    ok(pret == &fb2, "wrong return, expected %p got %p\n", &fb2, pret);
    ok(_read(fb1.fd, read_buffer, 1) == 0, "read failed\n");
    pret = (filebuf*) call_func1(p_filebuf_close, &fb2);
    ok(pret == &fb2, "wrong return, expected %p got %p\n", &fb2, pret);
    fb2.base.do_lock = -1;
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename2, OPENMODE_in|OPENMODE_noreplace, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename2, OPENMODE_trunc|OPENMODE_noreplace, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename3, OPENMODE_out|OPENMODE_nocreate|OPENMODE_noreplace, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);

    /* open protection*/
    fb3.fd = -1;
    fb3.base.do_lock = 0;
    pret = (filebuf*) call_func4(p_filebuf_open, &fb3, filename3, OPENMODE_in, filebuf_openprot);
    ok(pret == &fb3, "wrong return, expected %p got %p\n", &fb3, pret);
    fb2.base.do_lock = 0;
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename3, OPENMODE_in|OPENMODE_out, filebuf_openprot);
    ok(pret == &fb2, "wrong return, expected %p got %p\n", &fb2, pret);
    pret = (filebuf*) call_func1(p_filebuf_close, &fb2);
    ok(pret == &fb2, "wrong return, expected %p got %p\n", &fb2, pret);
    fb2.base.do_lock = -1;
    pret = (filebuf*) call_func1(p_filebuf_close, &fb3);
    ok(pret == &fb3, "wrong return, expected %p got %p\n", &fb3, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb3, filename3, OPENMODE_in, filebuf_sh_none);
    ok(pret == &fb3, "wrong return, expected %p got %p\n", &fb3, pret);
    pret = (filebuf*) call_func4(p_filebuf_open, &fb2, filename3, OPENMODE_in, filebuf_openprot);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    fb3.base.do_lock = -1;

    /* setmode */
    fb1.base.do_lock = 0;
    fb1.base.pbase = fb1.base.pptr = fb1.base.base;
    fb1.base.epptr = fb1.base.ebuf;
    ret = (int) call_func2(p_filebuf_setmode, &fb1, filebuf_binary);
    ok(ret == filebuf_text, "wrong return, expected %d got %d\n", filebuf_text, ret);
todo_wine
    ok(fb1.base.pptr == NULL, "wrong put pointer, expected %p got %p\n", NULL, fb1.base.pptr);
    ret = (int) call_func2(p_filebuf_setmode, &fb1, filebuf_binary);
    ok(ret == filebuf_binary, "wrong return, expected %d got %d\n", filebuf_binary, ret);
    fb1.base.do_lock = -1;
    ret = (int) call_func2(p_filebuf_setmode, &fb1, 0x9000);
    ok(ret == -1, "wrong return, expected -1 got %d\n", ret);
    fb2.base.do_lock = 0;
    ret = (int) call_func2(p_filebuf_setmode, &fb2, filebuf_text);
    ok(ret == -1, "wrong return, expected -1 got %d\n", ret);
    fb2.base.do_lock = -1;

    /* close */
    pret = (filebuf*) call_func1(p_filebuf_close, &fb2);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    fb3.base.do_lock = 0;
    pret = (filebuf*) call_func1(p_filebuf_close, &fb3);
    ok(pret == &fb3, "wrong return, expected %p got %p\n", &fb3, pret);
    ok(fb3.fd == -1, "wrong fd, expected -1 got %d\n", fb3.fd);
    fb3.fd = 5;
    pret = (filebuf*) call_func1(p_filebuf_close, &fb3);
    ok(pret == NULL, "wrong return, expected %p got %p\n", NULL, pret);
    ok(fb3.fd == 5, "wrong fd, expected 5 got %d\n", fb3.fd);
    fb3.base.do_lock = -1;

    SetEvent(lock_arg.test);
    WaitForSingleObject(thread, INFINITE);

    /* destructor */
    call_func1(p_filebuf_dtor, &fb1);
    ok(fb1.fd == -1, "wrong fd, expected -1 got %d\n", fb1.fd);
    call_func1(p_filebuf_dtor, &fb2);
    call_func1(p_filebuf_dtor, &fb3);

    ok(_unlink(filename1) == 0, "Couldn't unlink file named '%s'\n", filename1);
    ok(_unlink(filename2) == 0, "Couldn't unlink file named '%s'\n", filename2);
    ok(_unlink(filename3) == 0, "Couldn't unlink file named '%s'\n", filename3);
    CloseHandle(lock_arg.lock);
    CloseHandle(lock_arg.test);
    CloseHandle(thread);
}

struct ios_lock_arg
{
    ios *ios_obj;
    HANDLE lock;
    HANDLE release[3];
};

static DWORD WINAPI lock_ios(void *arg)
{
    struct ios_lock_arg *lock_arg = arg;
    p_ios_lock(lock_arg->ios_obj);
    p_ios_lockbuf(lock_arg->ios_obj);
    p_ios_lockc();
    SetEvent(lock_arg->lock);
    WaitForSingleObject(lock_arg->release[0], INFINITE);
    p_ios_unlockc();
    WaitForSingleObject(lock_arg->release[1], INFINITE);
    p_ios_unlockbuf(lock_arg->ios_obj);
    WaitForSingleObject(lock_arg->release[2], INFINITE);
    p_ios_unlock(lock_arg->ios_obj);
    return 0;
}

static void test_ios(void)
{
    ios ios_obj, ios_obj2;
    streambuf *psb;
    struct ios_lock_arg lock_arg;
    HANDLE thread;
    BOOL locked;
    LONG *pret, expected, ret;
    void **pret2;
    int i;

    memset(&ios_obj, 0xab, sizeof(ios));
    memset(&ios_obj2, 0xab, sizeof(ios));
    psb = p_operator_new(sizeof(streambuf));
    ok(psb != NULL, "failed to allocate streambuf object\n");
    call_func1(p_streambuf_ctor, psb);

    /* constructor/destructor */
    ok(*p_ios_fLockcInit == 4, "expected 4 got %d\n", *p_ios_fLockcInit);
    call_func2(p_ios_sb_ctor, &ios_obj, NULL);
    ok(ios_obj.sb == NULL, "expected %p got %p\n", NULL, ios_obj.sb);
    ok(ios_obj.state == IOSTATE_badbit, "expected %x got %x\n", IOSTATE_badbit, ios_obj.state);
    ok(ios_obj.special[0] == 0, "expected 0 got %d\n", ios_obj.special[0]);
    ok(ios_obj.special[1] == 0, "expected 0 got %d\n", ios_obj.special[1]);
    ok(ios_obj.delbuf == 0, "expected 0 got %d\n", ios_obj.delbuf);
    ok(ios_obj.tie == NULL, "expected %p got %p\n", NULL, ios_obj.tie);
    ok(ios_obj.flags == 0, "expected 0 got %x\n", ios_obj.flags);
    ok(ios_obj.precision == 6, "expected 6 got %d\n", ios_obj.precision);
    ok(ios_obj.fill == ' ', "expected ' ' got %d\n", ios_obj.fill);
    ok(ios_obj.width == 0, "expected 0 got %d\n", ios_obj.width);
    ok(ios_obj.do_lock == -1, "expected -1 got %d\n", ios_obj.do_lock);
    ok(ios_obj.lock.LockCount == -1, "expected -1 got %d\n", ios_obj.lock.LockCount);
    ok(*p_ios_fLockcInit == 5, "expected 5 got %d\n", *p_ios_fLockcInit);
    ios_obj.state = 0x8;
    call_func1(p_ios_dtor, &ios_obj);
    ok(ios_obj.state == IOSTATE_badbit, "expected %x got %x\n", IOSTATE_badbit, ios_obj.state);
    ok(*p_ios_fLockcInit == 4, "expected 4 got %d\n", *p_ios_fLockcInit);
    ios_obj.state = 0x8;
    call_func2(p_ios_sb_ctor, &ios_obj, psb);
    ok(ios_obj.sb == psb, "expected %p got %p\n", psb, ios_obj.sb);
    ok(ios_obj.state == IOSTATE_goodbit, "expected %x got %x\n", IOSTATE_goodbit, ios_obj.state);
    ok(ios_obj.delbuf == 0, "expected 0 got %d\n", ios_obj.delbuf);
    ok(*p_ios_fLockcInit == 5, "expected 5 got %d\n", *p_ios_fLockcInit);
    ios_obj.state = 0x8;
    call_func1(p_ios_dtor, &ios_obj);
    ok(ios_obj.sb == NULL, "expected %p got %p\n", NULL, ios_obj.sb);
    ok(ios_obj.state == IOSTATE_badbit, "expected %x got %x\n", IOSTATE_badbit, ios_obj.state);
    ok(*p_ios_fLockcInit == 4, "expected 4 got %d\n", *p_ios_fLockcInit);
    ios_obj.sb = psb;
    ios_obj.state = 0x8;
    call_func1(p_ios_ctor, &ios_obj);
    ok(ios_obj.sb == NULL, "expected %p got %p\n", NULL, ios_obj.sb);
    ok(ios_obj.state == IOSTATE_badbit, "expected %x got %x\n", IOSTATE_badbit, ios_obj.state);
    ok(*p_ios_fLockcInit == 5, "expected 5 got %d\n", *p_ios_fLockcInit);

    /* init */
    ios_obj.state |= 0x8;
    call_func2(p_ios_init, &ios_obj, psb);
    ok(ios_obj.sb == psb, "expected %p got %p\n", psb, ios_obj.sb);
    ok(ios_obj.state == 0x8, "expected %x got %x\n", 0x8, ios_obj.state);
    call_func2(p_ios_init, &ios_obj, NULL);
    ok(ios_obj.sb == NULL, "expected %p got %p\n", NULL, ios_obj.sb);
    ok(ios_obj.state == (0x8|IOSTATE_badbit), "expected %x got %x\n", (0x8|IOSTATE_badbit), ios_obj.state);
    ios_obj.sb = psb;
    ios_obj.delbuf = 0;
    call_func2(p_ios_init, &ios_obj, psb);
    ok(ios_obj.sb == psb, "expected %p got %p\n", psb, ios_obj.sb);
    ok(ios_obj.state == 0x8, "expected %x got %x\n", 0x8, ios_obj.state);
    call_func1(p_ios_dtor, &ios_obj);

    /* copy constructor */
    call_func2(p_ios_copy_ctor, &ios_obj, &ios_obj2);
    ok(ios_obj.sb == NULL, "expected %p got %p\n", NULL, ios_obj.sb);
    ok(ios_obj.state == (ios_obj2.state|IOSTATE_badbit), "expected %x got %x\n", (ios_obj2.state|IOSTATE_badbit), ios_obj.state);
    ok(ios_obj.delbuf == 0, "expected 0 got %d\n", ios_obj.delbuf);
    ok(ios_obj.tie == ios_obj2.tie, "expected %p got %p\n", ios_obj2.tie, ios_obj.tie);
    ok(ios_obj.flags == ios_obj2.flags, "expected %x got %x\n", ios_obj2.flags, ios_obj.flags);
    ok(ios_obj.precision == (char)0xab, "expected %d got %d\n", (char)0xab, ios_obj.precision);
    ok(ios_obj.fill == (char)0xab, "expected %d got %d\n", (char)0xab, ios_obj.fill);
    ok(ios_obj.width == (char)0xab, "expected %d got %d\n", (char)0xab, ios_obj.width);
    ok(ios_obj.do_lock == -1, "expected -1 got %d\n", ios_obj.do_lock);
    ok(*p_ios_fLockcInit == 5, "expected 5 got %d\n", *p_ios_fLockcInit);

    /* assignment */
    ios_obj.state = 0x8;
    ios_obj.delbuf = 2;
    ios_obj.tie = NULL;
    ios_obj.flags = 0;
    ios_obj.precision = 6;
    ios_obj.fill = ' ';
    ios_obj.width = 0;
    ios_obj.do_lock = 2;
    call_func2(p_ios_assign, &ios_obj, &ios_obj2);
    ok(ios_obj.sb == NULL, "expected %p got %p\n", NULL, ios_obj.sb);
    ok(ios_obj.state == (ios_obj2.state|IOSTATE_badbit), "expected %x got %x\n", (ios_obj2.state|IOSTATE_badbit), ios_obj.state);
    ok(ios_obj.delbuf == 2, "expected 2 got %d\n", ios_obj.delbuf);
    ok(ios_obj.tie == ios_obj2.tie, "expected %p got %p\n", ios_obj2.tie, ios_obj.tie);
    ok(ios_obj.flags == ios_obj2.flags, "expected %x got %x\n", ios_obj2.flags, ios_obj.flags);
    ok(ios_obj.precision == (char)0xab, "expected %d got %d\n", (char)0xab, ios_obj.precision);
    ok(ios_obj.fill == (char)0xab, "expected %d got %d\n", (char)0xab, ios_obj.fill);
    ok(ios_obj.width == (char)0xab, "expected %d got %d\n", (char)0xab, ios_obj.width);
    ok(ios_obj.do_lock == 2, "expected 2 got %d\n", ios_obj.do_lock);

    /* locking */
    ios_obj.sb = psb;
    ios_obj.do_lock = 1;
    ios_obj.sb->do_lock = 0;
    p_ios_clrlock(&ios_obj);
    ok(ios_obj.do_lock == 1, "expected 1 got %d\n", ios_obj.do_lock);
    ok(ios_obj.sb->do_lock == 1, "expected 1 got %d\n", ios_obj.sb->do_lock);
    ios_obj.do_lock = 0;
    p_ios_clrlock(&ios_obj);
    ok(ios_obj.do_lock == 1, "expected 1 got %d\n", ios_obj.do_lock);
    ok(ios_obj.sb->do_lock == 1, "expected 1 got %d\n", ios_obj.sb->do_lock);
    p_ios_setlock(&ios_obj);
    ok(ios_obj.do_lock == 0, "expected 0 got %d\n", ios_obj.do_lock);
    ok(ios_obj.sb->do_lock == 0, "expected 0 got %d\n", ios_obj.sb->do_lock);
    ios_obj.do_lock = -1;
    p_ios_setlock(&ios_obj);
    ok(ios_obj.do_lock == -2, "expected -2 got %d\n", ios_obj.do_lock);
    ok(ios_obj.sb->do_lock == -1, "expected -1 got %d\n", ios_obj.sb->do_lock);

    lock_arg.ios_obj = &ios_obj;
    lock_arg.lock = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(lock_arg.lock != NULL, "CreateEventW failed\n");
    lock_arg.release[0] = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(lock_arg.release[0] != NULL, "CreateEventW failed\n");
    lock_arg.release[1] = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(lock_arg.release[1] != NULL, "CreateEventW failed\n");
    lock_arg.release[2] = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(lock_arg.release[2] != NULL, "CreateEventW failed\n");
    thread = CreateThread(NULL, 0, lock_ios, (void*)&lock_arg, 0, NULL);
    ok(thread != NULL, "CreateThread failed\n");
    WaitForSingleObject(lock_arg.lock, INFINITE);

    locked = TryEnterCriticalSection(&ios_obj.lock);
    ok(locked == 0, "the ios object was not locked before\n");
    locked = TryEnterCriticalSection(&ios_obj.sb->lock);
    ok(locked == 0, "the streambuf was not locked before\n");
    locked = TryEnterCriticalSection(p_ios_static_lock);
    ok(locked == 0, "the static critical section was not locked before\n");

    /* flags */
    ios_obj.flags = 0x8000;
    ret = (LONG) call_func1(p_ios_flags_get, &ios_obj);
    ok(ret == 0x8000, "expected %x got %x\n", 0x8000, ret);
    ret = (LONG) call_func2(p_ios_flags_set, &ios_obj, 0x444);
    ok(ret == 0x8000, "expected %x got %x\n", 0x8000, ret);
    ok(ios_obj.flags == 0x444, "expected %x got %x\n", 0x444, ios_obj.flags);
    ret = (LONG) call_func2(p_ios_flags_set, &ios_obj, 0);
    ok(ret == 0x444, "expected %x got %x\n", 0x444, ret);
    ok(ios_obj.flags == 0, "expected %x got %x\n", 0, ios_obj.flags);

    /* setf */
    ios_obj.do_lock = 0;
    ios_obj.flags = 0x8400;
    ret = (LONG) call_func2(p_ios_setf, &ios_obj, 0x444);
    ok(ret == 0x8400, "expected %x got %x\n", 0x8400, ret);
    ok(ios_obj.flags == 0x8444, "expected %x got %x\n", 0x8444, ios_obj.flags);
    ret = (LONG) call_func3(p_ios_setf_mask, &ios_obj, 0x111, 0);
    ok(ret == 0x8444, "expected %x got %x\n", 0x8444, ret);
    ok(ios_obj.flags == 0x8444, "expected %x got %x\n", 0x8444, ios_obj.flags);
    ret = (LONG) call_func3(p_ios_setf_mask, &ios_obj, 0x111, 0x105);
    ok(ret == 0x8444, "expected %x got %x\n", 0x8444, ret);
    ok(ios_obj.flags == 0x8541, "expected %x got %x\n", 0x8541, ios_obj.flags);

    /* unsetf */
    ret = (LONG) call_func2(p_ios_unsetf, &ios_obj, 0x1111);
    ok(ret == 0x8541, "expected %x got %x\n", 0x8541, ret);
    ok(ios_obj.flags == 0x8440, "expected %x got %x\n", 0x8440, ios_obj.flags);
    ret = (LONG) call_func2(p_ios_unsetf, &ios_obj, 0x8008);
    ok(ret == 0x8440, "expected %x got %x\n", 0x8440, ret);
    ok(ios_obj.flags == 0x440, "expected %x got %x\n", 0x440, ios_obj.flags);
    ios_obj.do_lock = -1;

    /* state */
    ios_obj.state = 0x8;
    ret = (LONG) call_func1(p_ios_good, &ios_obj);
    ok(ret == 0, "expected 0 got %d\n", ret);
    ios_obj.state = IOSTATE_goodbit;
    ret = (LONG) call_func1(p_ios_good, &ios_obj);
    ok(ret == 1, "expected 1 got %d\n", ret);
    ret = (LONG) call_func1(p_ios_bad, &ios_obj);
    ok(ret == 0, "expected 0 got %d\n", ret);
    ios_obj.state = (IOSTATE_eofbit|IOSTATE_badbit);
    ret = (LONG) call_func1(p_ios_bad, &ios_obj);
    ok(ret == IOSTATE_badbit, "expected 4 got %d\n", ret);
    ret = (LONG) call_func1(p_ios_eof, &ios_obj);
    ok(ret == IOSTATE_eofbit, "expected 1 got %d\n", ret);
    ios_obj.state = 0x8;
    ret = (LONG) call_func1(p_ios_eof, &ios_obj);
    ok(ret == 0, "expected 0 got %d\n", ret);
    ret = (LONG) call_func1(p_ios_fail, &ios_obj);
    ok(ret == 0, "expected 0 got %d\n", ret);
    ios_obj.state = IOSTATE_badbit;
    ret = (LONG) call_func1(p_ios_fail, &ios_obj);
    ok(ret == IOSTATE_badbit, "expected 4 got %d\n", ret);
    ios_obj.state = (IOSTATE_eofbit|IOSTATE_failbit);
    ret = (LONG) call_func1(p_ios_fail, &ios_obj);
    ok(ret == IOSTATE_failbit, "expected 2 got %d\n", ret);
    ios_obj.state = (IOSTATE_eofbit|IOSTATE_failbit|IOSTATE_badbit);
    ret = (LONG) call_func1(p_ios_fail, &ios_obj);
    ok(ret == (IOSTATE_failbit|IOSTATE_badbit), "expected 6 got %d\n", ret);
    ios_obj.do_lock = 0;
    call_func2(p_ios_clear, &ios_obj, 0);
    ok(ios_obj.state == IOSTATE_goodbit, "expected 0 got %d\n", ios_obj.state);
    call_func2(p_ios_clear, &ios_obj, 0x8|IOSTATE_eofbit);
    ok(ios_obj.state == (0x8|IOSTATE_eofbit), "expected 9 got %d\n", ios_obj.state);
    ios_obj.do_lock = -1;

    SetEvent(lock_arg.release[0]);

    /* bitalloc */
    expected = 0x10000;
    for (i = 0; i < 20; i++) {
        ret = p_ios_bitalloc();
        ok(ret == expected, "expected %x got %x\n", expected, ret);
        ok(*p_ios_maxbit == expected, "expected %x got %x\n", expected, *p_ios_maxbit);
        expected <<= 1;
    }

    /* xalloc/pword/iword */
    ok(*p_ios_curindex == -1, "expected -1 got %d\n", *p_ios_curindex);
    expected = 0;
    for (i = 0; i < 8; i++) {
        ret = p_ios_xalloc();
        ok(ret == expected, "expected %d got %d\n", expected, ret);
        ok(*p_ios_curindex == expected, "expected %d got %d\n", expected, *p_ios_curindex);
        pret = (LONG*) call_func2(p_ios_iword, &ios_obj, ret);
        ok(pret == &p_ios_statebuf[ret], "expected %p got %p\n", &p_ios_statebuf[ret], pret);
        ok(*pret == 0, "expected 0 got %d\n", *pret);
        pret2 = (void**) call_func2(p_ios_pword, &ios_obj, ret);
        ok(pret2 == (void**)&p_ios_statebuf[ret], "expected %p got %p\n", (void**)&p_ios_statebuf[ret], pret2);
        expected++;
    }
    ret = p_ios_xalloc();
    ok(ret == -1, "expected -1 got %d\n", ret);
    ok(*p_ios_curindex == 7, "expected 7 got %d\n", *p_ios_curindex);

    SetEvent(lock_arg.release[1]);
    SetEvent(lock_arg.release[2]);
    WaitForSingleObject(thread, INFINITE);

    ios_obj.delbuf = 1;
    call_func1(p_ios_dtor, &ios_obj);
    ok(ios_obj.state == IOSTATE_badbit, "expected %x got %x\n", IOSTATE_badbit, ios_obj.state);
    ok(*p_ios_fLockcInit == 4, "expected 4 got %d\n", *p_ios_fLockcInit);
    CloseHandle(lock_arg.lock);
    CloseHandle(lock_arg.release[0]);
    CloseHandle(lock_arg.release[1]);
    CloseHandle(lock_arg.release[2]);
    CloseHandle(thread);
}

START_TEST(msvcirt)
{
    if(!init())
        return;

    test_streambuf();
    test_filebuf();
    test_ios();

    FreeLibrary(msvcrt);
    FreeLibrary(msvcirt);
}
