/*
 * STLinkUSBDriver.dll
 *
 * Generated from STLinkUSBDriver.dll by winedump.
 *
 * DO NOT SEND GENERATED DLLS FOR INCLUSION INTO WINE !
 *
 */
#ifndef __WINE_STLINKUSBDRIVER_DLL_H
#define __WINE_STLINKUSBDRIVER_DLL_H

#include "windef.h"
#include "wine/debug.h"
#include "winbase.h"
#include "winnt.h"

#define USB_ST_VID 0x483
#define USB_STLINK_PID 0x3744
#define USB_STLINK_32L_PID 0x3748
#define USB_STLINK_V21_PID 0x374b

#define STLINK_OK 1
#define STLINK_ERROR 4
#define STLINK_FAILED 0xe4

#define  DEFAULT_SENSE_LEN  14

#define REQUEST_WRITE    0
#define REQUEST_READ_EP1 1
#define REQUEST_READ_EP3 3

#define STLINK_V1  0
#define STLINK_V2  1
#define STLINK_V21 2


#include "pshpack1.h"
typedef struct {
  BYTE CDBLength;         // Command header length (CDB on mass storage)
  BYTE CDBByte[16];       // Command header (CDB on mass storage)
  BYTE InputRequest;      // REQUEST_WRITE, REQUEST_READ_EP1 or REQUEST_READ_EP3
  PVOID Buffer;           // Data for data stage (if any)
  DWORD BufferLength;     // Size of data stage (0 if no data stage)
  BYTE SenseLength;       // To be initialized: DEFAULT_SENSE_LEN
  BYTE Sense[DEFAULT_SENSE_LEN+2];
} TDeviceRequest, *PDeviceRequest;
#include "poppack.h"

/* __stdcall STLINKUSBDRIVER_STMass_Enum_GetNbDevices(); */
/* __stdcall STLINKUSBDRIVER_STMass_Enum_GetDevice(); */
/* __stdcall STLINKUSBDRIVER_STMass_GetDeviceInfo(); */
/* __stdcall STLINKUSBDRIVER_STMass_OpenDevice(); */
/* __stdcall STLINKUSBDRIVER_STMass_CloseDevice(); */
DWORD __stdcall STLINKUSBDRIVER_STMass_SendCommand(DWORD idx, DWORD handle, PDeviceRequest req, DWORD to);
DWORD __stdcall STLINKUSBDRIVER_STMass_Enum_Reenumerate(void);



#endif	/* __WINE_STLINKUSBDRIVER_DLL_H */
