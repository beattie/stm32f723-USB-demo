#pragma once
#include <stdint.h>

/*
 * USB-C device bring-up — OTG_HS full-speed device mode
 * Internal FS PHY on PB14 (DM) / PB15 (DP)
 * Clock: HSI48 + CRS (auto-trimmed to USB SOF)
 *
 * Enumerates as a vendor-specific device (VID=0xDEAD, PID=0xF723).
 * Verify with: lsusb -v   or   dmesg | tail
 */

void usb_dev_init(void);  /* call once at startup — includes HSI48+CRS clock setup */
void usb_dev_poll(void);  /* call from main loop as fast as possible */
