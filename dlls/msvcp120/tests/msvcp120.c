/*
 * Copyright 2014 Yifu Wang for ESRI
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

#include <locale.h>
#include <stdio.h>

#include "wine/test.h"
#include "winbase.h"

typedef int MSVCRT_long;

/* xtime */
typedef struct {
    __time64_t sec;
    MSVCRT_long nsec;
} xtime;

typedef struct {
    unsigned page;
    int mb_max;
    int unk;
    BYTE isleadbyte[32];
} _Cvtvec;

static inline const char* debugstr_longlong(ULONGLONG ll)
{
    static char string[17];
    if (sizeof(ll) > sizeof(unsigned long) && ll >> 32)
        sprintf(string, "%lx%08lx", (unsigned long)(ll >> 32), (unsigned long)ll);
    else
        sprintf(string, "%lx", (unsigned long)ll);
    return string;
}

static char* (__cdecl *p_setlocale)(int, const char*);
static int (__cdecl *p__setmbcp)(int);
static int (__cdecl *p_isleadbyte)(int);

static MSVCRT_long (__cdecl *p__Xtime_diff_to_millis2)(const xtime*, const xtime*);
static int (__cdecl *p_xtime_get)(xtime*, int);
static _Cvtvec* (__cdecl *p__Getcvt)(_Cvtvec*);
static void (CDECL *p__Call_once)(int *once, void (CDECL *func)(void));
static void (CDECL *p__Call_onceEx)(int *once, void (CDECL *func)(void*), void *argv);
static void (CDECL *p__Do_call)(void *this);

/* filesystem */
static ULONGLONG(__cdecl *p_tr2_sys__File_size)(char const*);

static HMODULE msvcp;
#define SETNOFAIL(x,y) x = (void*)GetProcAddress(msvcp,y)
#define SET(x,y) do { SETNOFAIL(x,y); ok(x != NULL, "Export '%s' not found\n", y); } while(0)
static BOOL init(void)
{
    HANDLE msvcr;

    msvcp = LoadLibraryA("msvcp120.dll");
    if(!msvcp)
    {
        win_skip("msvcp120.dll not installed\n");
        return FALSE;
    }

    SET(p__Xtime_diff_to_millis2,
            "_Xtime_diff_to_millis2");
    SET(p_xtime_get,
            "xtime_get");
    SET(p__Getcvt,
            "_Getcvt");
    SET(p__Call_once,
            "_Call_once");
    SET(p__Call_onceEx,
            "_Call_onceEx");
    SET(p__Do_call,
            "_Do_call");
    if(sizeof(void*) == 8) { /* 64-bit initialization */
        SET(p_tr2_sys__File_size,
                "?_File_size@sys@tr2@std@@YA_KPEBD@Z");
    } else {
        SET(p_tr2_sys__File_size,
                "?_File_size@sys@tr2@std@@YA_KPBD@Z");
    }

    msvcr = GetModuleHandleA("msvcr120.dll");
    p_setlocale = (void*)GetProcAddress(msvcr, "setlocale");
    p__setmbcp = (void*)GetProcAddress(msvcr, "_setmbcp");
    p_isleadbyte = (void*)GetProcAddress(msvcr, "isleadbyte");
    return TRUE;
}

static void test__Xtime_diff_to_millis2(void)
{
    struct {
        __time64_t sec_before;
        MSVCRT_long nsec_before;
        __time64_t sec_after;
        MSVCRT_long nsec_after;
        MSVCRT_long expect;
    } tests[] = {
        {1, 0, 2, 0, 1000},
        {0, 1000000000, 0, 2000000000, 1000},
        {1, 100000000, 2, 100000000, 1000},
        {1, 100000000, 1, 200000000, 100},
        {0, 0, 0, 1000000000, 1000},
        {0, 0, 0, 1200000000, 1200},
        {0, 0, 0, 1230000000, 1230},
        {0, 0, 0, 1234000000, 1234},
        {0, 0, 0, 1234100000, 1235},
        {0, 0, 0, 1234900000, 1235},
        {0, 0, 0, 1234010000, 1235},
        {0, 0, 0, 1234090000, 1235},
        {0, 0, 0, 1234000001, 1235},
        {0, 0, 0, 1234000009, 1235},
        {0, 0, -1, 0, 0},
        {0, 0, 0, -10000000, 0},
        {0, 0, -1, -100000000, 0},
        {-1, 0, 0, 0, 1000},
        {0, -100000000, 0, 0, 100},
        {-1, -100000000, 0, 0, 1100},
        {0, 0, -1, 2000000000, 1000},
        {0, 0, -2, 2000000000, 0},
        {0, 0, -2, 2100000000, 100}
    };
    int i;
    MSVCRT_long ret;
    xtime t1, t2;

    for(i = 0; i < sizeof(tests) / sizeof(tests[0]); ++ i)
    {
        t1.sec = tests[i].sec_before;
        t1.nsec = tests[i].nsec_before;
        t2.sec = tests[i].sec_after;
        t2.nsec = tests[i].nsec_after;
        ret = p__Xtime_diff_to_millis2(&t2, &t1);
        ok(ret == tests[i].expect,
                "_Xtime_diff_to_millis2(): test: %d expect: %d, got: %d\n",
                i, tests[i].expect, ret);
    }
}

static void test_xtime_get(void)
{
    static const MSVCRT_long tests[] = {1, 50, 100, 200, 500};
    MSVCRT_long diff;
    xtime before, after;
    int i;

    for(i = 0; i < sizeof(tests) / sizeof(tests[0]); i ++)
    {
        p_xtime_get(&before, 1);
        Sleep(tests[i]);
        p_xtime_get(&after, 1);

        diff = p__Xtime_diff_to_millis2(&after, &before);

        ok(diff >= tests[i],
                "xtime_get() not functioning correctly, test: %d, expect: ge %d, got: %d\n",
                i, tests[i], diff);
    }

    /* Test parameter and return value */
    before.sec = 0xdeadbeef, before.nsec = 0xdeadbeef;
    i = p_xtime_get(&before, 0);
    ok(i == 0, "expect xtime_get() to return 0, got: %d\n", i);
    ok(before.sec == 0xdeadbeef && before.nsec == 0xdeadbeef,
            "xtime_get() shouldn't have modified the xtime struct with the given option\n");

    before.sec = 0xdeadbeef, before.nsec = 0xdeadbeef;
    i = p_xtime_get(&before, 1);
    ok(i == 1, "expect xtime_get() to return 1, got: %d\n", i);
    ok(before.sec != 0xdeadbeef && before.nsec != 0xdeadbeef,
            "xtime_get() should have modified the xtime struct with the given option\n");
}

static void test__Getcvt(void)
{
    _Cvtvec cvtvec;
    int i;

    p__Getcvt(&cvtvec);
    ok(cvtvec.page == 0, "cvtvec.page = %d\n", cvtvec.page);
    ok(cvtvec.mb_max == 1, "cvtvec.mb_max = %d\n", cvtvec.mb_max);
    todo_wine ok(cvtvec.unk == 1, "cvtvec.unk = %d\n", cvtvec.unk);
    for(i=0; i<32; i++)
        ok(cvtvec.isleadbyte[i] == 0, "cvtvec.isleadbyte[%d] = %x\n", i, cvtvec.isleadbyte[i]);

    if(!p_setlocale(LC_ALL, ".936")) {
        win_skip("_Getcvt tests\n");
        return;
    }
    p__Getcvt(&cvtvec);
    ok(cvtvec.page == 936, "cvtvec.page = %d\n", cvtvec.page);
    ok(cvtvec.mb_max == 2, "cvtvec.mb_max = %d\n", cvtvec.mb_max);
    ok(cvtvec.unk == 0, "cvtvec.unk = %d\n", cvtvec.unk);
    for(i=0; i<32; i++)
        ok(cvtvec.isleadbyte[i] == 0, "cvtvec.isleadbyte[%d] = %x\n", i, cvtvec.isleadbyte[i]);

    p__setmbcp(936);
    p__Getcvt(&cvtvec);
    ok(cvtvec.page == 936, "cvtvec.page = %d\n", cvtvec.page);
    ok(cvtvec.mb_max == 2, "cvtvec.mb_max = %d\n", cvtvec.mb_max);
    ok(cvtvec.unk == 0, "cvtvec.unk = %d\n", cvtvec.unk);
    for(i=0; i<32; i++) {
        BYTE b = 0;
        int j;

        for(j=0; j<8; j++)
            b |= (p_isleadbyte(i*8+j) ? 1 : 0) << j;
        ok(cvtvec.isleadbyte[i] ==b, "cvtvec.isleadbyte[%d] = %x (%x)\n", i, cvtvec.isleadbyte[i], b);
    }
}

static int cnt;
static int once;

static void __cdecl call_once_func(void)
{
    ok(!once, "once != 0\n");
    cnt += 0x10000;
}

static void __cdecl call_once_ex_func(void *arg)
{
    int *i = arg;

    ok(!once, "once != 0\n");
    (*i)++;
}

static DWORD WINAPI call_once_thread(void *arg)
{
    p__Call_once(&once, call_once_func);
    return 0;
}

static DWORD WINAPI call_once_ex_thread(void *arg)
{
    p__Call_onceEx(&once, call_once_ex_func, &cnt);
    return 0;
}

static void test__Call_once(void)
{
    HANDLE h[4];
    int i;

    for(i=0; i<4; i++)
        h[i] = CreateThread(NULL, 0, call_once_thread, &once, 0, NULL);
    ok(WaitForMultipleObjects(4, h, TRUE, INFINITE) == WAIT_OBJECT_0,
            "error waiting for all threads to finish\n");
    ok(cnt == 0x10000, "cnt = %x\n", cnt);
    ok(once == 1, "once = %x\n", once);

    once = cnt = 0;
    for(i=0; i<4; i++)
        h[i] = CreateThread(NULL, 0, call_once_ex_thread, &once, 0, NULL);
    ok(WaitForMultipleObjects(4, h, TRUE, INFINITE) == WAIT_OBJECT_0,
            "error waiting for all threads to finish\n");
    ok(cnt == 1, "cnt = %x\n", cnt);
    ok(once == 1, "once = %x\n", once);
}

static void **vtbl_func0;
#ifdef __i386__
/* TODO: this should be a __thiscall function */
static void __stdcall thiscall_func(void)
{
    cnt = 1;
}
#else
static void __cdecl thiscall_func(void *this)
{
    ok(this == &vtbl_func0, "incorrect this value\n");
    cnt = 1;
}
#endif

static void test__Do_call(void)
{
    void *pfunc = thiscall_func;

    cnt = 0;
    vtbl_func0 = &pfunc;
    p__Do_call(&vtbl_func0);
    ok(cnt == 1, "func was not called\n");
}

static void test_tr2_sys__File_size(void)
{
    ULONGLONG val;
    HANDLE file;
    LARGE_INTEGER file_size = {{7, 0}};
    CreateDirectoryA("tr2_test_dir", NULL);

    file = CreateFileA("tr2_test_dir/f1", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "create file failed: INVALID_HANDLE_VALUE\n");
    ok(SetFilePointerEx(file, file_size, NULL, FILE_BEGIN), "SetFilePointerEx failed\n");
    ok(SetEndOfFile(file), "SetEndOfFile failed\n");
    CloseHandle(file);
    val = p_tr2_sys__File_size("tr2_test_dir/f1");
    ok(val == 7, "file_size is %s\n", debugstr_longlong(val));

    file = CreateFileA("tr2_test_dir/f2", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "create file failed: INVALID_HANDLE_VALUE\n");
    CloseHandle(file);
    val = p_tr2_sys__File_size("tr2_test_dir/f2");
    ok(val == 0, "file_size is %s\n", debugstr_longlong(val));

    val = p_tr2_sys__File_size("tr2_test_dir");
    ok(val == 0, "file_size is %s\n", debugstr_longlong(val));

    errno = 0xdeadbeef;
    val = p_tr2_sys__File_size("tr2_test_dir/not_exists_file");
    ok(val == 0, "file_size is %s\n", debugstr_longlong(val));
    ok(errno == 0xdeadbeef, "errno = %d\n", errno);

    errno = 0xdeadbeef;
    val = p_tr2_sys__File_size(NULL);
    ok(val == 0, "file_size is %s\n", debugstr_longlong(val));
    ok(errno == 0xdeadbeef, "errno = %d\n", errno);

    ok(DeleteFileA("tr2_test_dir/f1"), "Expected tr2_test_dir/f1 to exist\n");
    ok(DeleteFileA("tr2_test_dir/f2"), "Expected tr2_test_dir/f2 to exist\n");
    ok(RemoveDirectoryA("tr2_test_dir"), "Expected tr2_test_dir to exist\n");
}

START_TEST(msvcp120)
{
    if(!init()) return;
    test__Xtime_diff_to_millis2();
    test_xtime_get();
    test__Getcvt();
    test__Call_once();
    test__Do_call();

    test_tr2_sys__File_size();
    FreeLibrary(msvcp);
}
