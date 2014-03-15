/*
 * STLinkUSBDriver.dll
 *
 * Generated from STLinkUSBDriver.dll by winedump.
 *
 * DO NOT SUBMIT GENERATED DLLS FOR INCLUSION INTO WINE!
 *
 */

#include "config.h"

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "STLinkUSBDriver_dll.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(stlink);
#ifdef HAVE_LIBUSB_H
#include <libusb-1.0/libusb.h>
static libusb_context* libusb_ctx = NULL;
typedef struct {
    int type;
    libusb_device *dev;
    libusb_device_handle *handle;
} Tstlink_dev, *Pstlink_dev;
static  Tstlink_dev *stlink_dev = NULL;
static struct libusb_transfer* req_trans = NULL;
static struct libusb_transfer* rep_trans = NULL;
static int sg_index = 0;
#endif


struct trans_ctx {
#define TRANS_FLAGS_IS_DONE (1 << 0)
#define TRANS_FLAGS_HAS_ERROR (1 << 1)
    volatile unsigned long flags;
};

static void on_trans_done(struct libusb_transfer * trans)
{
    struct trans_ctx * const ctx = trans->user_data;

    if (trans->status != LIBUSB_TRANSFER_COMPLETED)
    {
        if(trans->status == LIBUSB_TRANSFER_TIMED_OUT)
        {
            WARN("Timeout\n");
//            ctx->flags = 0;
//            libusb_submit_transfer(trans);
//            return;
        }
        else if (trans->status == LIBUSB_TRANSFER_CANCELLED)
            WARN("cancelled\n");
        else if (trans->status == LIBUSB_TRANSFER_NO_DEVICE)
            WARN("no device\n");
        else
            WARN("unknown\n");
        ctx->flags |= TRANS_FLAGS_HAS_ERROR;
    }
    ctx->flags |= TRANS_FLAGS_IS_DONE;
}

int submit_wait(struct libusb_transfer * trans) {
    struct timeval start;
    struct timeval now;
    struct timeval diff;
    struct trans_ctx trans_ctx;
    enum libusb_error error;

    trans_ctx.flags = 0;

    /* brief intrusion inside the libusb interface */
    trans->callback = on_trans_done;
    trans->user_data = &trans_ctx;

    if ((error = libusb_submit_transfer(trans))) {
        WARN("libusb_submit_transfer(%d)\n", error);
        return -1;
    }

    gettimeofday(&start, NULL);

    while (trans_ctx.flags == 0) {
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        if (libusb_handle_events_timeout(libusb_ctx, &timeout)) {
            WARN("libusb_handle_events()\n");
            return -1;
        }

        gettimeofday(&now, NULL);
        timersub(&now, &start, &diff);
        if (diff.tv_sec >= 1) {
            libusb_cancel_transfer(trans);
            WARN("libusb_handle_events() timeout\n");
            return -1;
        }
    }

    if (trans_ctx.flags & TRANS_FLAGS_HAS_ERROR) {
        WARN("libusb_handle_events() | has_error\n");
        return -1;
    }

    return 0;
}

DWORD send_recv(DWORD idx,
                BYTE *txbuf, DWORD txsize,
                BYTE *rxbuf, DWORD rxsize)
{
    int res = 0;
    int ep_tx = 2;
    libusb_device_handle *handle = (&stlink_dev)[idx]->handle;

    if ((&stlink_dev)[idx]->type == STLINK_V21)
        ep_tx = 1;
    if( txsize) {
        libusb_fill_bulk_transfer(req_trans, handle,
                                  ep_tx | LIBUSB_ENDPOINT_OUT,
                                  txbuf, txsize,
                                  NULL, NULL,
                                  0
            );

        if (submit_wait(req_trans)) {
            WARN("clear 2\n");
            libusb_clear_halt(handle,2);
            return -1;
        }
    }
    /* send_only */
    if (rxsize != 0) {

        /* read the response */

        libusb_fill_bulk_transfer(rep_trans, handle,
                                  0x01| LIBUSB_ENDPOINT_IN,
                                  (BYTE*)rxbuf, rxsize, NULL, NULL, 0);

        if (submit_wait(rep_trans)) {
            WARN("clear 1\n");
            libusb_clear_halt(handle,1);
            return -1;
        }
        res = rep_trans->actual_length;
        if (res >0) {
            int i;
            BYTE *p = (BYTE*)rxbuf;
            TRACE("Res ");
            for (i=0; i< res && i <32 ; i++)
                TRACE("%02x", p[i]);
            TRACE("\n");
        }
    }
    return res;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("(0x%p, %d, %p)\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason) {
    case DLL_WINE_PREATTACH:
        return TRUE;    /* prefer builtin version */
    case DLL_PROCESS_ATTACH:
        libusb_init(&libusb_ctx);
        DisableThreadLibraryCalls(hinstDLL);
        req_trans = libusb_alloc_transfer(0);
        rep_trans = libusb_alloc_transfer(0);
        if(!req_trans || !req_trans) {
            WARN("libusb_alloc_transfer failed\n");
            return FALSE;
        }
        break;
    case DLL_PROCESS_DETACH:
        if (stlink_dev) {
            int j;

            TRACE("Checking for devices to release\n");
            /* Release and close all open devices */
            for(j = 0; stlink_dev[j].dev != 0; j++) {
                if ( stlink_dev[j].handle) {
                    TRACE("Releasing idx %d\n", j);
                    libusb_release_interface( stlink_dev[j].handle, 1);
                    libusb_close(stlink_dev[j].handle);
                }
            }
            HeapFree(GetProcessHeap(), 0, stlink_dev);
            stlink_dev = NULL;
        }
        if (libusb_ctx)
            libusb_exit(libusb_ctx);
        break;
    }

    return TRUE;
}

/******************************************************************
 *		STMass_Enum_GetNbDevices (STLINKUSBDRIVER.1)
 *
 *
 */

DWORD __stdcall STLINKUSBDRIVER_STMass_Enum_GetNbDevices(void)
{
    libusb_device** devs = NULL;
    libusb_device* dev;
    int  count, i, idx = 0;
    DWORD nb_devs = 0;

    TRACE("\n");
    count = libusb_get_device_list(libusb_ctx, &devs);
    if (count < 0) {
        WARN("libusb_get_device_list\n");
        return 0;
    }

    TRACE(" Checking %d devices\n", count);
    for (i = 0; i < count; ++i) {
        struct libusb_device_descriptor desc;
        dev = devs[i];
        if (libusb_get_device_descriptor(dev, &desc)) {
            WARN("libusb_get_device_descriptor failed\n");
            return STLINK_ERROR;
        }
        if ((desc.idVendor == USB_ST_VID) &&
            ((desc.idProduct == USB_STLINK_32L_PID) ||
             (desc.idProduct == USB_STLINK_V21_PID) ||
             (desc.idProduct == USB_STLINK_PID ))) {
            nb_devs++;
        }
    }
    if (nb_devs) {
        int len = (nb_devs + 1) * sizeof(Tstlink_dev);
        TRACE("Found %d devices\n", nb_devs);
        if (stlink_dev) {
            int j;

            TRACE("Checking for devices to release\n");
            /* Release and close all open devices */
            for(j = 0; stlink_dev[j].dev != 0; j++) {
                if ( stlink_dev[j].handle) {
                    TRACE("Releasing idx %d\n", j);
                    libusb_release_interface( stlink_dev[j].handle, 1);
                    libusb_close(stlink_dev[j].handle);
                }
            }
            HeapFree(GetProcessHeap(), 0, stlink_dev);
            stlink_dev = NULL;
        }
        stlink_dev = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
        for (i = 0; i < count; ++i) {
            struct libusb_device_descriptor desc;
            dev = devs[i];
            if (libusb_get_device_descriptor(dev, &desc)) {
                WARN("libusb_get_device_descriptor failed\n");
                return STLINK_ERROR;
            }
            if (desc.idVendor == USB_ST_VID) {
                switch (desc.idProduct) {
                case USB_STLINK_PID:
                    stlink_dev[idx].dev = dev;
                    stlink_dev[idx].type = STLINK_V1;
                    idx++;
                    break;
                case USB_STLINK_32L_PID:
                    stlink_dev[idx].dev = dev;
                    stlink_dev[idx].type = STLINK_V2;
                    idx++;
                    break;
                case USB_STLINK_V21_PID:
                    stlink_dev[idx].dev = dev;
                    stlink_dev[idx].type = STLINK_V21;
                    idx++;
                    break;
                }
            }
        }
    }
//    libusb_free_device_list(devs, 1);
    TRACE("%d devs\n", nb_devs);
    return nb_devs;
}

/******************************************************************
 *		STMass_Enum_GetDevice (STLINKUSBDRIVER.2)
 *
 *
 */
DWORD __stdcall STLINKUSBDRIVER_STMass_Enum_GetDevice(DWORD idx, DWORD *x)
{
    TRACE(" dev %d %p\n", idx, x);
    *x = idx;
    return STLINK_OK;
}
/******************************************************************
 *		STMass_GetDeviceInfo (STLINKUSBDRIVER.3)
 *
 *
 */
DWORD __stdcall STLINKUSBDRIVER_STMass_GetDeviceInfo(void)
{
    TRACE("\n");
    return STLINK_OK;
}
/******************************************************************
 *		STMass_OpenDevice (STLINKUSBDRIVER.4)
 *
 *
 */
DWORD __stdcall STLINKUSBDRIVER_STMass_OpenDevice(DWORD idx, DWORD * handle)
{
    int config;

    TRACE(" %d dev %p idx %d\n", idx, (&stlink_dev)[idx]->dev, idx);

    if ((&stlink_dev)[idx]->handle)
        return STLINK_OK;
    if (libusb_open((&stlink_dev)[idx]->dev, &(&stlink_dev)[idx]->handle)) {
        WARN("libusb_open\n");
        return STLINK_ERROR;
    }
    if (libusb_get_configuration((&stlink_dev)[idx]->handle, &config)) {
        WARN("libusb_set_configuration\n");
        return STLINK_ERROR;
    }
    if (config != 1) {
        TRACE("setting new configuration (%d -> 1)\n", config);
        if (libusb_set_configuration((&stlink_dev)[idx]->handle, 1)) {
            /* this may fail for a previous configured device */
            WARN("libusb_set_configuration() failed\n");
            return STLINK_ERROR;
        }
    }

    if (libusb_claim_interface ((&stlink_dev)[idx]->handle, 0)) {
        WARN("libusb_claim\n");
        return STLINK_ERROR;
    }
    *handle = idx;
    TRACE("ret\n");
    return STLINK_OK;
}
/******************************************************************
 *		STMass_CloseDevice (STLINKUSBDRIVER.5)
 *
 *
 */
DWORD __stdcall STLINKUSBDRIVER_STMass_CloseDevice(DWORD x, DWORD HANDLE)
{
    TRACE("%d\n", x);
    libusb_close((&stlink_dev)[x]->handle);
    (&stlink_dev)[x]->handle = NULL;
    return STLINK_OK;
}
/******************************************************************
*		STMass_SendCommand (STLINKUSBDRIVER.6)
*
*
*/
DWORD __stdcall STLINKUSBDRIVER_STMass_SendCommand(
    DWORD idx, DWORD handle, PDeviceRequest req, DWORD to)
{
    int i;
    char version[] = "unknown";

    DWORD actual_len = ((&stlink_dev)[idx]->type == STLINK_V1)?req->CDBLength: 16;
//    DWORD actual_len = req->CDBLength;

    switch ((&stlink_dev)[idx]->type) {
    case STLINK_V1: strcpy(version,"STLINK_V1");
        break;
    case STLINK_V2: strcpy(version,"STLINK_V2");
        break;
    case STLINK_V21: strcpy(version,"STLINK_V21");
        break;
    }

    TRACE("idx %d handle %d req %p to %x Type %s\n", idx, handle, req, to, version);
    TRACE("header len %d ", req->CDBLength);
    for (i=0; i< req->CDBLength && i < 32; i++)
        TRACE(" %02x", req->CDBByte[i]);
    TRACE("Type %d buf %p buf len %d sense len %d\n",
          req->InputRequest,
          req->Buffer,
          req->BufferLength,
          req->SenseLength);

    if ((&stlink_dev)[idx]->type == STLINK_V1) {
        BYTE scsi[31];
        scsi[0]= 'U';
        scsi[1]= 'S';
        scsi[2]= 'B';
        scsi[3]= 'C';
        scsi[4] = sg_index & 0xff;
        scsi[5] = sg_index>>8 & 0xff;
        scsi[6] = sg_index>>16 & 0xff;
        scsi[7] = sg_index>>24 & 0xff;
        scsi[8] = req->BufferLength   & 0xff;
        scsi[9] = req->BufferLength>>8 & 0xff;
        scsi[10] = req->BufferLength>>16 & 0xff;
        scsi[11] = req->BufferLength>>24 & 0xff;
        scsi[12] = (req->InputRequest == REQUEST_READ_EP1)?0x80:0;
        scsi[13] = 0;
        scsi[14] = req->CDBLength;
        for(i=0; i< req->CDBLength; i++)
            scsi[15+i] = req->CDBByte[i];
        memset(scsi + 15 + req->CDBLength, 0, sizeof(scsi) - 15 - req->CDBLength);
        if (req->InputRequest == REQUEST_READ_EP1) {

            send_recv(idx, scsi, 31,  (BYTE*) req->Buffer, req->BufferLength);
            send_recv(idx, NULL, 0, req->Sense, req->SenseLength);
        }
        else if (req->InputRequest == REQUEST_WRITE) {
            send_recv(idx, scsi, 31, NULL, 0);
            send_recv(idx,  (BYTE*) req->Buffer, req->BufferLength, NULL, 0);
            send_recv(idx, NULL, 0, req->Sense, req->SenseLength);
        }
        else
            TRACE("Unhandled request\n");
    }
    else {
        if (req->InputRequest == REQUEST_READ_EP1)
            send_recv(idx, req->CDBByte, actual_len, (BYTE*) req->Buffer, req->BufferLength);
        else if (req->InputRequest == REQUEST_WRITE) {
            send_recv(idx, req->CDBByte, actual_len, NULL, 0);
            send_recv(idx,  (BYTE*) req->Buffer, req->BufferLength, NULL, 0);
            
        }
        else
            TRACE("Unhandled request\n");

    }
    return STLINK_OK;
}
/******************************************************************
 *		STMass_Enum_Reenumerate (STLINKUSBDRIVER.7)
 *
 *
 */
DWORD __stdcall STLINKUSBDRIVER_STMass_Enum_Reenumerate(void)
{
    TRACE("\n");
    return STLINK_OK;
}
