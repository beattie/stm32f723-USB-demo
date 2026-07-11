#pragma once
#include <stdint.h>

/*
 * MicroSD bring-up via SDMMC1
 * PC8=D0, PC9=D1, PC10=D2, PC11=D3, PC12=CLK (AF12)
 * PD2=CMD (AF12)
 * PE3=CDET (active-low input, card present when low)
 *
 * Initialises card to TRAN state at 1-bit, 400 kHz.
 * LED3 (PE1) on when a card is present and initialised.
 */

void sdcard_init(void);   /* call once at startup */
void sdcard_poll(void);   /* call from main loop */
int  sdcard_ready(void);  /* 1 = card present and initialised */
