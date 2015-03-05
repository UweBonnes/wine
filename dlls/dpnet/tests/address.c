/*
 * Copyright 2014 Alistair Leslie-Hughes
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

#define WIN32_LEAN_AND_MEAN
#include <stdio.h>

#include <dplay8.h>
#include "wine/test.h"

/* {6733C6E8-A0D6-450E-8C18-CEACF331DC27} */
static const GUID IID_Random = {0x6733c6e8, 0xa0d6, 0x450e, { 0x8c, 0x18, 0xce, 0xac, 0xf3, 0x31, 0xdc, 0x27 } };

static void create_directplay_address(void)
{
    HRESULT hr;
    IDirectPlay8Address *localaddr = NULL;

    hr = CoCreateInstance( &CLSID_DirectPlay8Address, NULL, CLSCTX_ALL, &IID_IDirectPlay8Address, (LPVOID*)&localaddr);
    ok(hr == S_OK, "Failed to create IDirectPlay8Address object\n");
    if(SUCCEEDED(hr))
    {
        GUID guidsp;

        hr = IDirectPlay8Address_GetSP(localaddr, NULL);
        ok(hr == DPNERR_INVALIDPOINTER, "GetSP failed 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetSP(localaddr, &guidsp);
        ok(hr == DPNERR_DOESNOTEXIST, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_SetSP(localaddr, &GUID_NULL);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetSP(localaddr, &guidsp);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(IsEqualGUID(&guidsp, &GUID_NULL), "wrong guid: %s\n", wine_dbgstr_guid(&guidsp));

        hr = IDirectPlay8Address_SetSP(localaddr, &IID_Random);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetSP(localaddr, &guidsp);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(IsEqualGUID(&guidsp, &IID_Random), "wrong guid: %s\n", wine_dbgstr_guid(&guidsp));

        hr = IDirectPlay8Address_SetSP(localaddr, &CLSID_DP8SP_TCPIP);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetSP(localaddr, &guidsp);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(IsEqualGUID(&guidsp, &CLSID_DP8SP_TCPIP), "wrong guid: %s\n", wine_dbgstr_guid(&guidsp));

        IDirectPlay8Address_Release(localaddr);
    }
}

static void address_addcomponents(void)
{
    static const WCHAR UNKNOWN[] = { 'u','n','k','n','o','w','n',0 };
    static const WCHAR localhost[] = {'l','o','c','a','l','h','o','s','t',0};
    static const char testing[] = "testing";
    HRESULT hr;
    IDirectPlay8Address *localaddr = NULL;

    hr = CoCreateInstance( &CLSID_DirectPlay8Address, NULL, CLSCTX_ALL, &IID_IDirectPlay8Address, (LPVOID*)&localaddr);
    ok(hr == S_OK, "Failed to create IDirectPlay8Address object\n");
    if(SUCCEEDED(hr))
    {
        GUID compguid;
        DWORD size, type;
        DWORD components;
        DWORD i;
        DWORD namelen = 0;
        DWORD bufflen = 0;
        DWORD port = 8888;
        WCHAR buffer[256];

        /* We can add any Component to the Address interface not just the predefined ones. */
        hr = IDirectPlay8Address_AddComponent(localaddr, UNKNOWN, &IID_Random, sizeof(GUID), DPNA_DATATYPE_GUID);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, UNKNOWN, &IID_Random, sizeof(GUID)+1, DPNA_DATATYPE_GUID);
        ok(hr == DPNERR_INVALIDPARAM, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_HOSTNAME, &localhost, sizeof(localhost)+2, DPNA_DATATYPE_STRING);
        ok(hr == DPNERR_INVALIDPARAM, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_HOSTNAME, &localhost, sizeof(localhost)/2, DPNA_DATATYPE_STRING);
        ok(hr == DPNERR_INVALIDPARAM, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_HOSTNAME, testing, sizeof(testing)+2, DPNA_DATATYPE_STRING_ANSI);
        ok(hr == DPNERR_INVALIDPARAM, "got 0x%08x\n", hr);

        /* Show that on error, nothing is added. */
        size = sizeof(buffer);
        hr = IDirectPlay8Address_GetComponentByName(localaddr, DPNA_KEY_HOSTNAME, buffer, &size, &type);
        ok(hr == DPNERR_DOESNOTEXIST, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_HOSTNAME, testing, sizeof(testing), DPNA_DATATYPE_STRING_ANSI);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_PORT, &port, sizeof(DWORD)+2, DPNA_DATATYPE_DWORD);
        ok(hr == DPNERR_INVALIDPARAM, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_HOSTNAME, &localhost, sizeof(localhost), DPNA_DATATYPE_STRING);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        /* The information doesn't get removed when invalid parameters are used.*/
        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_HOSTNAME, &localhost, sizeof(localhost)+2, DPNA_DATATYPE_STRING);
        ok(hr == DPNERR_INVALIDPARAM, "got 0x%08x\n", hr);

        size = sizeof(buffer);
        hr = IDirectPlay8Address_GetComponentByName(localaddr, DPNA_KEY_HOSTNAME, buffer, &size, &type);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(type == DPNA_DATATYPE_STRING, "incorrect type %d\n", type);
        todo_wine ok(!lstrcmpW(buffer, localhost), "Invalid string: %s\n", wine_dbgstr_w(buffer));

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_PORT, &port, sizeof(DWORD)+2, DPNA_DATATYPE_DWORD);
        ok(hr == DPNERR_INVALIDPARAM, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_AddComponent(localaddr, DPNA_KEY_PORT, &port, sizeof(DWORD), DPNA_DATATYPE_DWORD);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetComponentByName(localaddr, NULL, &compguid, &size, &type);
        ok(hr == E_POINTER, "got 0x%08x\n", hr);

        size = sizeof(GUID)-1;
        hr = IDirectPlay8Address_GetComponentByName(localaddr, UNKNOWN, NULL, &size, &type);
        ok(hr == E_POINTER, "got 0x%08x\n", hr);

        size = sizeof(GUID);
        hr = IDirectPlay8Address_GetComponentByName(localaddr, UNKNOWN, NULL, &size, &type);
        ok(hr == E_POINTER, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetComponentByName(localaddr, UNKNOWN, &compguid, NULL, &type);
        ok(hr == E_POINTER, "got 0x%08x\n", hr);

        size = sizeof(GUID)-1;
        hr = IDirectPlay8Address_GetComponentByName(localaddr, UNKNOWN, &compguid, &size, NULL);
        ok(hr == E_POINTER, "got 0x%08x\n", hr);

        size = sizeof(GUID);
        hr = IDirectPlay8Address_GetComponentByName(localaddr, UNKNOWN, &compguid, &size, NULL);
        ok(hr == E_POINTER, "got 0x%08x\n", hr);

        size = sizeof(GUID)-1;
        hr = IDirectPlay8Address_GetComponentByName(localaddr, UNKNOWN, &compguid, &size, &type);
        ok(hr == DPNERR_BUFFERTOOSMALL, "got 0x%08x\n", hr);
        ok(size == sizeof(GUID), "got %d\n", size);

        size = sizeof(GUID);
        hr = IDirectPlay8Address_GetComponentByName(localaddr, UNKNOWN, &compguid, &size, &type);
        ok(IsEqualGUID(&compguid, &IID_Random), "incorrect guid\n");
        ok(size == sizeof(GUID), "incorrect size got %d\n", size);
        ok(type == DPNA_DATATYPE_GUID, "incorrect type\n");
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetNumComponents(localaddr, NULL);
        ok(hr == DPNERR_INVALIDPOINTER, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetNumComponents(localaddr, &components);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetComponentByIndex(localaddr, 100, NULL, &namelen, NULL, &bufflen, &type);
        todo_wine ok(hr == DPNERR_DOESNOTEXIST, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetComponentByIndex(localaddr, 100, NULL, NULL, NULL, &bufflen, &type);
        todo_wine ok(hr == E_POINTER, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetComponentByIndex(localaddr, 100, NULL, &namelen, NULL, NULL, &type);
        todo_wine ok(hr == E_POINTER, "got 0x%08x\n", hr);

        trace("GetNumComponents=%d\n", components);
        for(i=0; i < components; i++)
        {
            WCHAR *name;
            void *buffer;

            bufflen = 0;
            namelen = 0;

            hr = IDirectPlay8Address_GetComponentByIndex(localaddr, i, NULL, &namelen, NULL, &bufflen, &type);
            todo_wine ok(hr == DPNERR_BUFFERTOOSMALL, "got 0x%08x\n", hr);

            name =  HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, namelen * sizeof(WCHAR));
            buffer =  HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufflen);

            hr = IDirectPlay8Address_GetComponentByIndex(localaddr, i, name, &namelen, buffer, &bufflen, &type);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            if(hr == S_OK)
            {
                switch(type)
                {
                    case DPNA_DATATYPE_STRING:
                        trace("%d: %s: %s\n", i, wine_dbgstr_w(name), wine_dbgstr_w(buffer));
                        break;
                    case DPNA_DATATYPE_DWORD:
                        trace("%d: %s: %d\n", i, wine_dbgstr_w(name), *(DWORD*)buffer);
                        break;
                    case DPNA_DATATYPE_GUID:
                        trace("%d: %s: %s\n", i, wine_dbgstr_w(name), wine_dbgstr_guid( (GUID*)buffer));
                        break;
                    case DPNA_DATATYPE_BINARY:
                        trace("%d: %s: Binary Data %d\n", i, wine_dbgstr_w(name), bufflen);
                        break;
                    default:
                        trace(" Unknown\n");
                        break;
                }
            }

            HeapFree(GetProcessHeap(), 0, name);
            HeapFree(GetProcessHeap(), 0, buffer);
        }

        IDirectPlay8Address_Release(localaddr);
    }
}

static void address_setsp(void)
{
    HRESULT hr;
    IDirectPlay8Address *localaddr = NULL;

    hr = CoCreateInstance( &CLSID_DirectPlay8Address, NULL, CLSCTX_ALL, &IID_IDirectPlay8Address, (LPVOID*)&localaddr);
    ok(hr == S_OK, "Failed to create IDirectPlay8Address object\n");
    if(SUCCEEDED(hr))
    {
        DWORD components;
        WCHAR *name;
        GUID  guid = IID_Random;
        DWORD type;
        DWORD namelen = 0;
        DWORD bufflen = 0;

        hr = IDirectPlay8Address_GetNumComponents(localaddr, &components);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(components == 0, "components=%d\n", components);

        hr = IDirectPlay8Address_SetSP(localaddr, &CLSID_DP8SP_TCPIP);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetNumComponents(localaddr, &components);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(components == 1, "components=%d\n", components);

        hr = IDirectPlay8Address_GetComponentByIndex(localaddr, 0, NULL, &namelen, NULL, &bufflen, &type);
        todo_wine ok(hr == DPNERR_BUFFERTOOSMALL, "got 0x%08x\n", hr);

        name =  HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, namelen * sizeof(WCHAR));

        hr = IDirectPlay8Address_GetComponentByIndex(localaddr, 0, name, &namelen, (void*)&guid, &bufflen, &type);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        todo_wine ok(type == DPNA_DATATYPE_GUID, "wrong datatype: %d\n", type);
        todo_wine ok(IsEqualGUID(&guid, &CLSID_DP8SP_TCPIP), "wrong guid\n");

        HeapFree(GetProcessHeap(), 0, name);

        IDirectPlay8Address_Release(localaddr);
    }
}

static void address_duplicate(void)
{
    HRESULT hr;
    IDirectPlay8Address *localaddr = NULL;
    IDirectPlay8Address *duplicate = NULL;
    DWORD components, dupcomps;
    GUID  guid = IID_Random;

    hr = CoCreateInstance( &CLSID_DirectPlay8Address, NULL, CLSCTX_ALL, &IID_IDirectPlay8Address, (LPVOID*)&localaddr);
    ok(hr == S_OK, "Failed to create IDirectPlay8Address object\n");
    if(SUCCEEDED(hr))
    {
        hr = IDirectPlay8Address_SetSP(localaddr, &CLSID_DP8SP_TCPIP);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDirectPlay8Address_GetNumComponents(localaddr, &components);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(components == 1, "components=%d\n", components);

        hr = IDirectPlay8Address_Duplicate(localaddr, &duplicate);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        if(SUCCEEDED(hr))
        {
            hr = IDirectPlay8Address_GetSP(duplicate, &guid);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(IsEqualGUID(&guid, &CLSID_DP8SP_TCPIP), "wrong guid\n");

            hr = IDirectPlay8Address_GetNumComponents(duplicate, &dupcomps);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(components == dupcomps, "expected %d got %d\n", components, dupcomps);

            IDirectPlay8Address_Release(duplicate);
        }

        IDirectPlay8Address_Release(localaddr);
    }
}

START_TEST(address)
{
    HRESULT hr;

    hr = CoInitialize(0);
    ok(hr == S_OK, "failed to init com\n");
    if(hr != S_OK)
        return;

    create_directplay_address();
    address_addcomponents();
    address_setsp();
    address_duplicate();

    CoUninitialize();
}
