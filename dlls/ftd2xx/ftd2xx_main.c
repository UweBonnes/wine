/*
 * FTD2XX.dll
 *
 * Forward calls to ftd2xx.dll to libftd2xx.so
 *
 * Copyright 2009 Uwe Bonnes
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
 *
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/library.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ftd2xx);

#define LIBFTD2XX_NAME "libftd2xx.so"

typedef DWORD                   *FT_HANDLE;
typedef ULONG                    FT_STATUS;
typedef struct _ft_device_list_info_node {
        ULONG Flags;
    ULONG Type;
        ULONG ID;
        DWORD LocId;
        char SerialNumber[16];
        char Description[64];
        FT_HANDLE ftHandle;
} FT_DEVICE_LIST_INFO_NODE;

/* Device status */

enum {
  FT_OK,
  FT_INVALID_HANDLE,
  FT_DEVICE_NOT_FOUND,
  FT_DEVICE_NOT_OPENED,
  FT_IO_ERROR,
  FT_INSUFFICIENT_RESOURCES,
  FT_INVALID_PARAMETER,
  FT_INVALID_BAUD_RATE,
  FT_DEVICE_NOT_OPENED_FOR_ERASE,
  FT_DEVICE_NOT_OPENED_FOR_WRITE,
  FT_FAILED_TO_WRITE_DEVICE,
  FT_EEPROM_READ_FAILED,
  FT_EEPROM_WRITE_FAILED,
  FT_EEPROM_ERASE_FAILED,
  FT_EEPROM_NOT_PRESENT,
  FT_EEPROM_NOT_PROGRAMMED,
  FT_INVALID_ARGS,
  FT_NOT_SUPPORTED,
  FT_OTHER_ERROR
};
typedef ULONG   FT_DEVICE;

enum {
  FT_DEVICE_BM,
  FT_DEVICE_AM,
  FT_DEVICE_100AX,
  FT_DEVICE_UNKNOWN,
  FT_DEVICE_2232C,
  FT_DEVICE_232R,
  FT_DEVICE_2232H,
  FT_DEVICE_4232H
};

const char * res2string(FT_STATUS res )
{
  switch (res)
    {
    case FT_OK: return "FT_OK";
    case FT_INVALID_HANDLE:  return "FT_INVALID_HANDLE";
    case FT_DEVICE_NOT_FOUND: return "FT_DEVICE_NOT_FOUND";
    case FT_DEVICE_NOT_OPENED: return "FT_DEVICE_NOT_OPENED";
    case FT_IO_ERROR: return "FT_IO_ERROR";
    case FT_INSUFFICIENT_RESOURCES: return "FT_INSUFFICIENT_RESOURCES";
    case FT_INVALID_PARAMETER: return "FT_INVALID_PARAMETER";
    case FT_INVALID_BAUD_RATE: return "FT_INVALID_BAUD_RATE";
    case FT_DEVICE_NOT_OPENED_FOR_ERASE: return "FT_DEVICE_NOT_OPENED_FOR_ERASE";
    case FT_FAILED_TO_WRITE_DEVICE: return "FT_FAILED_TO_WRITE_DEVICE";
    case FT_EEPROM_READ_FAILED: return "FT_EEPROM_READ_FAILED";
    case FT_EEPROM_WRITE_FAILED: return "FT_EEPROM_WRITE_FAILED";
    case FT_EEPROM_ERASE_FAILED: return "FT_EEPROM_ERASE_FAILED";
    case FT_EEPROM_NOT_PRESENT: return "FT_EEPROM_NOT_PRESENT";
    case FT_EEPROM_NOT_PROGRAMMED: return "FT_EEPROM_NOT_PROGRAMMED";
    case FT_INVALID_ARGS: return "FT_INVALID_ARGS";
    case FT_NOT_SUPPORTED: return "FT_NOT_SUPPORTED";
    case FT_OTHER_ERROR: return "FT_OTHER_ERROR";
    default: return "Unknown";
    }
}

static FT_STATUS (*pFT_ListDevices)(PVOID, PVOID, DWORD) = NULL;

static FT_STATUS (*pFT_Open)(int, FT_HANDLE *) = NULL;
static FT_STATUS (*pFT_Close)(FT_HANDLE) = NULL;
static FT_STATUS (*pFT_Read)(FT_HANDLE, LPVOID, DWORD, LPDWORD) = NULL;
static FT_STATUS (*pFT_Write)(FT_HANDLE, LPVOID, DWORD, LPDWORD) = NULL;
static FT_STATUS (*pFT_OpenEx)(PVOID, DWORD, FT_HANDLE *) = NULL;
static FT_STATUS (*pFT_ResetDevice)(FT_HANDLE) = NULL;
static FT_STATUS (*pFT_SetChars)(FT_HANDLE, UCHAR, UCHAR, UCHAR, UCHAR) = NULL;
static FT_STATUS (*pFT_Purge)(FT_HANDLE, ULONG) = NULL;
static FT_STATUS (*pFT_GetQueueStatus)(FT_HANDLE, DWORD*) = NULL;
static FT_STATUS (*pFT_SetLatencyTimer)(FT_HANDLE, UCHAR) = NULL;
static FT_STATUS (*pFT_SetBitMode)(FT_HANDLE, UCHAR, UCHAR) = NULL;
static FT_STATUS (*pFT_SetTimeouts)(FT_HANDLE, ULONG, ULONG) = NULL;
static FT_STATUS (*pFT_ReadEE)(FT_HANDLE, DWORD, LPWORD) = NULL;
static FT_STATUS (*pFT_GetDeviceInfo)(FT_HANDLE, FT_DEVICE *, LPDWORD,
                                      PCHAR, PCHAR, LPVOID) = NULL;
static FT_STATUS (*pFT_WriteEE)(FT_HANDLE, DWORD, WORD) = NULL;
static FT_STATUS (*pFT_EraseEE)(FT_HANDLE) = NULL;
static FT_STATUS (*pFT_SetUSBParameters)(FT_HANDLE, ULONG, ULONG) = NULL;
static FT_STATUS (*pFT_CreateDeviceInfoList)(LPDWORD) = NULL;
static FT_STATUS (*pFT_GetDeviceInfoList)(FT_DEVICE_LIST_INFO_NODE *, LPDWORD) = NULL;
static FT_STATUS (*pFT_GetDeviceInfoDetail)(DWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPVOID, LPVOID, FT_HANDLE*) = NULL;

static void *ftd2xx_handle = NULL;

static int load_functions(void)
{
  if (pFT_ListDevices) /* loaded already */
    return 0;
  ftd2xx_handle = wine_dlopen(LIBFTD2XX_NAME, RTLD_NOW, NULL, 0);
    if(!ftd2xx_handle) {
      FIXME("Wine cannot find native library %s, ftd2xx.dll not working.\n",
            LIBFTD2XX_NAME);
        return 1;
    }
#define LOAD_FUNCPTR(f) if((p##f = wine_dlsym(ftd2xx_handle, #f, NULL, 0))\
                           == NULL){WARN("Can't find symbol %s\n", #f); \
        return 1;}
LOAD_FUNCPTR(FT_ListDevices);
LOAD_FUNCPTR(FT_Open);
LOAD_FUNCPTR(FT_Close);
LOAD_FUNCPTR(FT_Read);
LOAD_FUNCPTR(FT_Write);
LOAD_FUNCPTR(FT_OpenEx);
LOAD_FUNCPTR(FT_ResetDevice);
LOAD_FUNCPTR(FT_SetChars);
LOAD_FUNCPTR(FT_Purge);
LOAD_FUNCPTR(FT_GetQueueStatus);
LOAD_FUNCPTR(FT_SetLatencyTimer);
LOAD_FUNCPTR(FT_SetBitMode);
LOAD_FUNCPTR(FT_SetTimeouts);
LOAD_FUNCPTR(FT_ReadEE);
LOAD_FUNCPTR(FT_GetDeviceInfo);
LOAD_FUNCPTR(FT_WriteEE);
LOAD_FUNCPTR(FT_EraseEE);
LOAD_FUNCPTR(FT_SetUSBParameters);
LOAD_FUNCPTR(FT_CreateDeviceInfoList);
LOAD_FUNCPTR(FT_GetDeviceInfoList);
LOAD_FUNCPTR(FT_GetDeviceInfoDetail);
#undef LOAD_FUNCPTR

 return 0;
}

static void unload_functions(void)
{
  pFT_ListDevices = NULL;
  pFT_Open = NULL;
  pFT_Close = NULL;
  pFT_OpenEx = NULL;
  pFT_ResetDevice = NULL;
  pFT_SetChars = NULL;
  pFT_Purge = NULL;
  pFT_Read = NULL;
  pFT_Write = NULL;
  pFT_GetQueueStatus = NULL;
  pFT_SetLatencyTimer = NULL;
  pFT_SetBitMode = NULL;
  pFT_SetTimeouts = NULL;
  pFT_ReadEE = NULL;
  pFT_SetUSBParameters= NULL;
  pFT_CreateDeviceInfoList= NULL;
  pFT_GetDeviceInfoList = NULL;
  pFT_GetDeviceInfoDetail= NULL;
  pFT_EraseEE = NULL;
  if (ftd2xx_handle)
    wine_dlclose(ftd2xx_handle, NULL, 0);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  TRACE("(0x%p, %d, %p)\n", hinstDLL, fdwReason, lpvReserved);

  switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
      DisableThreadLibraryCalls(hinstDLL);
      /* Try to load low-level library */
      if (load_functions() != 0)
	return FALSE;  /* error */
      break;
    case DLL_PROCESS_DETACH:
      unload_functions();
      break;
    }

  return TRUE;
}

#define FT_OPEN_BY_SERIAL_NUMBER    1
#define FT_OPEN_BY_DESCRIPTION      2
#define FT_OPEN_BY_LOCATION         4

#define FT_LIST_NUMBER_ONLY                     0x80000000
#define FT_LIST_BY_INDEX                        0x40000000
#define FT_LIST_ALL                             0x20000000

FT_STATUS WINAPI FTD2XX_FT_ListDevices(
        PVOID pArg1,
        PVOID pArg2,
        DWORD Flags
        )
{
  FT_STATUS res = pFT_ListDevices(pArg1, pArg2, Flags);
  TRACE("res %s Flag %08x\n",  res2string(res), Flags);
  if (Flags & FT_LIST_NUMBER_ONLY)
      TRACE("%d devices\n", *(DWORD*)pArg1);
  else if (Flags &  FT_LIST_BY_INDEX)
  {
      if (Flags & FT_OPEN_BY_SERIAL_NUMBER)
          TRACE("Index %d SERIAL %s\n", (DWORD)pArg1, (PCHAR)pArg2);
      else if (Flags & FT_OPEN_BY_DESCRIPTION)
          TRACE("Index %d DESC %s\n", (DWORD)pArg1, (PCHAR)pArg2);
  }
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_Open(
        int deviceNumber,
        FT_HANDLE *pHandle
        )
{
  FT_STATUS res =  pFT_Open(deviceNumber, pHandle);
  TRACE("res %s\n",  res2string(res));
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_Read(
    FT_HANDLE ftHandle,
    LPVOID lpBuffer,
    DWORD nBufferSize,
    LPDWORD lpBytesReturned
    )
{
    FT_STATUS res =  pFT_Read(ftHandle, lpBuffer, nBufferSize, lpBytesReturned);
    if (WINE_TRACE_ON(ftd2xx))
    {
        int i;
        TRACE("res %s for %d read %d\n",  res2string(res), nBufferSize,
              *lpBytesReturned);
        if (res == FT_OK)
            for(i=0; (i<*lpBytesReturned) && (i<100); i++)
                TRACE("%02x ",((PUCHAR)lpBuffer)[i]);
        TRACE("\n");
    }
    return res;
}

FT_STATUS WINAPI FTD2XX_FT_Write(
    FT_HANDLE ftHandle,
    LPVOID lpBuffer,
    DWORD nBufferSize,
    LPDWORD  lpBytesWritten
    )
{
    FT_STATUS res =  pFT_Write(ftHandle, lpBuffer, nBufferSize, lpBytesWritten);
    TRACE("res %s len %d writen %d\n",  res2string(res), nBufferSize,
          *lpBytesWritten);
    if (WINE_TRACE_ON(ftd2xx))
    {
        int i;
        for(i=0; (i<nBufferSize) && (i <100); i++)
            TRACE("%02x ",((PUCHAR)lpBuffer)[i]);
        TRACE("\n");
    }
    return res;
}

FT_STATUS WINAPI FTD2XX_FT_OpenEx(
        PVOID pArg1,
        DWORD Flags,
        FT_HANDLE *pHandle
        )
{

    FT_STATUS res;

    if (Flags & FT_OPEN_BY_LOCATION)
    {
        WARN("Must work around missing FT_OPEN_BY_LOCATION\n");
        res =   pFT_Open(0, pHandle);
    }
    else
        res =  pFT_OpenEx(pArg1, Flags, pHandle);
    TRACE("res %s\n",  res2string(res));
    return res;
}

FT_STATUS WINAPI FTD2XX_FT_ResetDevice(
       FT_HANDLE pHandle
        )
{
  FT_STATUS res =  pFT_ResetDevice(pHandle);
  TRACE("res %s\n",  res2string(res));
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_SetChars(
    FT_HANDLE ftHandle,
    UCHAR EventChar,
    UCHAR EventCharEnabled,
    UCHAR ErrorChar,
    UCHAR ErrorCharEnabled)
{
    FT_STATUS res =  pFT_SetChars(ftHandle, EventChar, EventCharEnabled,
                                  ErrorChar, ErrorCharEnabled);
    TRACE("res %s\n",  res2string(res));
    return res;
}

FT_STATUS WINAPI FTD2XX_FT_Purge(
    FT_HANDLE pHandle,
    ULONG mask
        )
{
    FT_STATUS res =  pFT_Purge(pHandle, mask);
    TRACE("mask 0x%08x res %s\n",  mask,res2string(res));
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_Close(
        FT_HANDLE ftHandle
        )
{
  FT_STATUS res =  pFT_Close(ftHandle);
  TRACE("res %s\n",  res2string(res));
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_GetQueueStatus
(
    FT_HANDLE ftHandle,
    DWORD *dwRxBytes
 )
{
    FT_STATUS res = pFT_GetQueueStatus(ftHandle, dwRxBytes);
    TRACE("res %s, %d bytes\n",  res2string(res), *dwRxBytes);
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_SetLatencyTimer
(
    FT_HANDLE ftHandle,
    UCHAR ucLatency
 )
{
    FT_STATUS res = pFT_SetLatencyTimer(ftHandle, ucLatency);
    TRACE("res %s to %d \n",  res2string(res), ucLatency);
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_SetBitMode
(
    FT_HANDLE ftHandle,
    UCHAR ucMask,
    UCHAR ucEnable
 )
{
    FT_STATUS res = pFT_SetBitMode(ftHandle, ucMask, ucEnable);
    TRACE("res %s, Mask %02x Enable %02x\n",  res2string(res),
          ucMask, ucEnable);
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_SetTimeouts
(
    FT_HANDLE ftHandle,
    ULONG ReadTimeout,
    ULONG WriteTimeout
 )
{
    FT_STATUS res = pFT_SetTimeouts(ftHandle, ReadTimeout, WriteTimeout);
    TRACE("res %s, Read %08x Write %08x\n",  res2string(res),
          ReadTimeout, WriteTimeout);
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_ReadEE
(
 FT_HANDLE ftHandle,
 DWORD dwWordOffset,
 LPWORD lpwValue
 )
{
  FT_STATUS res =  pFT_ReadEE(ftHandle, dwWordOffset, lpwValue);
  TRACE("res %s pos 0x%04x value 0x%04x\n",  res2string(res),
        dwWordOffset, *lpwValue);
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_GetDeviceInfo
(
 FT_HANDLE ftHandle,
 FT_DEVICE *lpftDevice,
 LPDWORD lpdwID,
 PCHAR SerialNumber,
 PCHAR Description,
 LPVOID Dummy
 )
{
  FT_STATUS res = pFT_GetDeviceInfo(ftHandle, lpftDevice, lpdwID,
                                    SerialNumber, Description, Dummy);
  TRACE("res %s\n",  res2string(res));
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_WriteEE
(
 FT_HANDLE ftHandle,
 DWORD dwWordOffset,
 WORD wValue
 )
{
  FT_STATUS res = pFT_WriteEE(ftHandle, dwWordOffset, wValue);
  TRACE("res %s pos 0x%04x value 0x%04x\n",  res2string(res),
        dwWordOffset, wValue);
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_EraseEE
(
 FT_HANDLE ftHandle
 )
{
  FT_STATUS res = pFT_EraseEE(ftHandle);
  TRACE("res %s\n",  res2string(res));
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_CreateDeviceInfoList
(
 LPDWORD devs
 )
{
  FT_STATUS res = pFT_CreateDeviceInfoList(devs);
  TRACE("res %s\n",  res2string(res));
  return res;
}

FT_STATUS WINAPI FTD2XX_FT_SetUSBParameters
(
    FT_HANDLE ftHandle,
    ULONG ulInTransferSize,
    ULONG ulOutTransferSize
 )
{
    FT_STATUS res = pFT_SetUSBParameters
        (ftHandle, ulInTransferSize, ulOutTransferSize);
    TRACE("res %s\n",  res2string(res));
    return res;
}

FT_STATUS WINAPI FTD2XX_FT_GetDeviceInfoList
(
    FT_DEVICE_LIST_INFO_NODE *pDest,
    LPDWORD lpdwNumDevs
    )
{
    FT_STATUS res = pFT_GetDeviceInfoList(pDest, lpdwNumDevs);
    TRACE("res %s\n",  res2string(res));
    return res;
}

FT_STATUS WINAPI FTD2XX_FT_GetDeviceInfoDetail
(
    WORD dwIndex,
    LPDWORD lpdwFlags,
    LPDWORD lpdwType,
    LPDWORD lpdwID,
    LPDWORD lpdwLocId,
    LPVOID lpSerialNumber,
    LPVOID lpDescription,
    FT_HANDLE *pftHandle
 )
{
    FT_STATUS res = pFT_GetDeviceInfoDetail
        (dwIndex, lpdwFlags, lpdwType, lpdwID, lpdwLocId, lpSerialNumber,
         lpDescription, pftHandle);
    TRACE("res %s\n", res2string(res));
    return res;
}
