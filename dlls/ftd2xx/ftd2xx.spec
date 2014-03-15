# Generated from FTD2XX.dll by winedump

@ stdcall FT_Open(long ptr) FTD2XX_FT_Open
@ stdcall FT_Close(long) FTD2XX_FT_Close
@ stdcall FT_Read(long ptr long ptr) FTD2XX_FT_Read
@ stdcall FT_Write(long ptr long ptr) FTD2XX_FT_Write
@ stub FT_IoCtl
@ stdcall FT_ResetDevice (long) FTD2XX_FT_ResetDevice
@ stub FT_SetBaudRate
@ stub FT_SetDataCharacteristics
@ stub FT_SetFlowControl
@ stub FT_SetDtr
@ stub FT_ClrDtr
@ stub FT_SetRts
@ stub FT_ClrRts
@ stub FT_GetModemStatus
@ stdcall FT_SetChars(long long long long long)FTD2XX_FT_SetChars
@ stdcall FT_Purge (long long) FTD2XX_FT_Purge
@ stdcall FT_SetTimeouts(long long long) FTD2XX_FT_SetTimeouts
@ stdcall FT_GetQueueStatus(long long) FTD2XX_FT_GetQueueStatus
@ stub FT_SetEventNotification
@ stub FT_GetEventStatus
@ stub FT_GetStatus
@ stub FT_SetBreakOn
@ stub FT_SetBreakOff
@ stub FT_SetWaitMask
@ stub FT_WaitOnMask
@ stub FT_SetDivisor
@ stdcall FT_OpenEx(ptr long ptr) FTD2XX_FT_OpenEx
@ stdcall FT_ListDevices(ptr ptr long) FTD2XX_FT_ListDevices
@ stdcall FT_SetLatencyTimer(long long) FTD2XX_FT_SetLatencyTimer
@ stub FT_GetLatencyTimer
@ stdcall FT_SetBitMode(long long long) FTD2XX_FT_SetBitMode
@ stub FT_GetBitMode
@ stdcall FT_SetUSBParameters(long long long) FTD2XX_FT_SetUSBParameters
@ stdcall FT_EraseEE(long) FTD2XX_FT_EraseEE
@ stdcall FT_ReadEE(long long ptr) FTD2XX_FT_ReadEE
@ stdcall FT_WriteEE(long long long) FTD2XX_FT_WriteEE
@ stub FT_EE_Program
@ stub FT_EE_Read
@ stub FT_EE_UARead
@ stub FT_EE_UASize
@ stub FT_EE_UAWrite
@ stub FT_W32_CreateFile
@ stub FT_W32_CloseHandle
@ stub FT_W32_ReadFile
@ stub FT_W32_WriteFile
@ stub FT_W32_GetOverlappedResult
@ stub FT_W32_ClearCommBreak
@ stub FT_W32_ClearCommError
@ stub FT_W32_EscapeCommFunction
@ stub FT_W32_GetCommModemStatus
@ stub FT_W32_GetCommState
@ stub FT_W32_GetCommTimeouts
@ stub FT_W32_GetLastError
@ stub FT_W32_PurgeComm
@ stub FT_W32_SetCommBreak
@ stub FT_W32_SetCommMask
@ stub FT_W32_SetCommState
@ stub FT_W32_SetCommTimeouts
@ stub FT_W32_SetupComm
@ stub FT_W32_WaitCommEvent
@ stdcall FT_GetDeviceInfo(long ptr ptr ptr ptr ptr) FTD2XX_FT_GetDeviceInfo
@ stub FT_W32_CancelIo
@ stub FT_StopInTask
@ stub FT_RestartInTask
@ stub FT_SetResetPipeRetryCount
@ stub FT_ResetPort
@ stub FT_EE_ProgramEx
@ stub FT_EE_ReadEx
@ stub FT_CyclePort
@ stdcall FT_CreateDeviceInfoList(ptr) FTD2XX_FT_CreateDeviceInfoList
@ stdcall FT_GetDeviceInfoList(ptr ptr) FTD2XX_FT_GetDeviceInfoList
@ stdcall FT_GetDeviceInfoDetail(long ptr ptr ptr ptr ptr ptr ptr)  FTD2XX_FT_GetDeviceInfoDetail
@ stub FT_SetDeadmanTimeout
@ stub FT_GetDriverVersion
@ stub FT_GetLibraryVersion
@ stub FT_W32_GetCommMask
@ stub FT_Rescan
@ stub FT_Reload
@ stub FT_GetComPortNumber
