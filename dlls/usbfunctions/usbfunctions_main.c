/*
 * USBFunctions.dll
 *
 * Generated from ../../../drive_c/windows/system32/USBFunctions.dll by winedump.
 *
 * DO NOT SUBMIT GENERATED DLLS FOR INCLUSION INTO WINE!
 *
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <stdio.h>

#include <libusb-1.0/libusb.h>


#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(usbfunctions);

static  struct libusb_device_handle *xpcu;

static BOOL write_internal_ram (libusb_device_handle  *udh, char *buf,
                    int start_addr, size_t len)
{
  int addr;
  int n;
  int a;
  int quanta = 64;

  TRACE("addr %x len %d\n", start_addr, len);

  for (addr = start_addr; addr < start_addr + (int) len; addr += quanta){
    n = len + start_addr - addr;
    if (n > quanta)
      n = quanta;

    a = libusb_control_transfer(udh, 0x40, 0xA0,
				addr, 0, (unsigned char*)(buf + (addr - start_addr)), n, 1000);

    if (a < 0){
      ERR("write_internal_ram failed: %d\n", a);
      return FALSE;
    }
  }
  return TRUE;
}

static int load_firmware(libusb_device_handle *udh, const char *filename)
{
  char s[1024];
  int length;
  int addr;
  int type;
  char data[256];
  unsigned char checksum, a;
  unsigned int b;
  int fd, i;
  int ret = -1;

  HANDLE h;

  FILE *f;

  h = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
  wine_server_handle_to_fd( h, FILE_READ_DATA, &fd, NULL );
  f = fdopen (fd, "ra");
  if (f == 0)
    {
      ERR("Can't open HEX File %s\n", filename);
      return -2;
    }

  s[0] = 1;
  if ( !write_internal_ram( udh, s, 0xe600, 1))
    {
      ERR("Can't Reset device\n");
      return -3;
    }
  while (!feof(f))
    {
      fgets(s, sizeof (s), f); /* we should not use more than 263 bytes normally */
      if(s[0]!=':'){
	ERR("%s: invalid line: \"%s\"\n", filename, s);
	goto fail;
      }
      sscanf(s+1, "%02x", &length);
      sscanf(s+3, "%04x", &addr);
      sscanf(s+7, "%02x", &type);

      if(type==0){

	a=length+(addr &0xff)+(addr>>8)+type;
	for(i=0;i<length;i++){
	  sscanf (s+9+i*2,"%02x", &b);
	  data[i]=b;
	  a=a+data[i];
	}

	sscanf (s+9+length*2,"%02x", &b);
	checksum=b;
	if (((a+checksum)&0xff)!=0x00){
	  ERR ("  ** Checksum failed: got 0x%02x versus 0x%02x\n", (-a)&0xff, checksum);
	  goto fail;
	}
	if (!write_internal_ram (udh, data, addr, length))
	  {
	    goto fail;
	  }
      }
      else if (type == 0x01){      // EOF
	break;
      }
      else if (type == 0x02){
	ERR( "Extended address: whatever I do with it?\n");
	ERR( "%s: invalid line: \"%s\"\n", filename, s);
	goto fail;
    }
  }

  s[0] = 0;
  if (!write_internal_ram( udh, s, 0xe600, 1))          // take CPU out of reset
    {
      ERR("Can't take device out of reset\n");
      ret = -5;
      goto fail;
    }

  fclose (f);
  return 0;

 fail:
  fclose (f);
  return ret;
}


BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("(0x%p, %d, %p)\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;    /* prefer builtin version */
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
	  if (xpcu)
	    libusb_close(xpcu);
            break;
    }

    return TRUE;
}

INT WINAPI LoadFirmware (char * name)
{
  int ret = 0;
  int i;

  TRACE("Dummy load of %s\n", name);
  ret = libusb_init(NULL);
  if (ret < 0)
    {
      TRACE( "failed to initialise libusb\n");
      return -100;
    }
  xpcu = libusb_open_device_with_vid_pid( NULL, 0x456, 0xb403);
  if (!xpcu)
    {
      WARN("ADF Device not found\n");
      return -1;
    }
  TRACE("loading\n");
  libusb_set_configuration(xpcu, 0);
  ret = load_firmware(xpcu, name);
  if (ret <0)
    return ret;
  TRACE("done\n");
  libusb_close(xpcu);
  for (i=0; 1; i++)
    {
      Sleep(100);
      xpcu = libusb_open_device_with_vid_pid( NULL, 0x456, 0xb403);
      if(i == 50)
	{
	  ERR(" Reenumweration failed\n");
	  return -1;
	}
      if (xpcu)
	break;
    }
  TRACE(" Reenum took %d tries\n", i);
  libusb_set_configuration(xpcu, 0);

  return 0;;
}

INT WINAPI VendorIORequest (int req , int lbits, int hbits,
			    int dir, int len, unsigned char * buf)
{
  int i;
  TRACE("VendorIORequest %x %d %d %d %d %p",
	req, lbits, hbits, dir, len, buf);
  for(i =0; i<len; i++)
    TRACE("%02x ", buf[i]);
  TRACE("\n");
  if((i =libusb_control_transfer(xpcu, 0x40, req, lbits, hbits, buf, len, 1000))<0)
    {
      TRACE("usb_control_msg %d\n", i);
      return 1;
    }
  return 0;
}
