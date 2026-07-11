#pragma once
#include <stdint.h>

/*
 * USB-C device bring-up via OTG_HS in FS device mode.
 * PB14 = OTG_HS_DM, PB15 = OTG_HS_DP (AF12).
 *
 * Bring-up goal: detect USB bus reset from host PC, confirming D+/D-
 * are connected.  LED3 (PE1) on when bus reset received.
 */

void usb_device_init(void);   /* call once at startup */
void usb_device_poll(void);   /* call from main loop */
int  usb_device_connected(void); /* 1 = USB reset received from host */
