#pragma once
#include <stdint.h>

/*
 * USB-A host bring-up — OTG_FS host mode
 * PA11 = OTG_FS_DM, PA12 = OTG_FS_DP (AF10)
 * VBUS supplied directly from USB-C 5V input; no GPIO switch needed.
 * Clock: HSI48 + CRS (also sets up CLK48 for SDMMC1).
 *
 * Verify with: lsusb   (Linux host sees the keyboard through the device side)
 * For bring-up: usb_host_connected() returns 1 when anything plugs into USB-A.
 */

void usb_host_init(void);   /* call once at startup */
void usb_host_poll(void);   /* call from main loop */
int  usb_host_connected(void); /* 1 = device present on USB-A port */
