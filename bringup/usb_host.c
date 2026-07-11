/*
 * usb_host.c — USB-A host bring-up on STM32F723
 *
 * Uses OTG_FS peripheral in host mode (base 0x50000000).
 * PA11 = OTG_FS_DM, PA12 = OTG_FS_DP (AF10).
 * VBUS is hardwired from USB-C 5V input — no GPIO switch needed.
 *
 * Clock: HSI48 + CRS (48 MHz USB clock, no external crystal).
 * CLK48SEL[27:26] = 11 in RCC_DCKCFGR2 (offset 0x90).
 *
 * Bring-up goal: usb_host_connected() returns 1 when anything plugs
 * into the USB-A port.  LED2 (PE0) reflects connect status.
 */

#include <stdint.h>
#include "usb_host.h"

/* ================================================================
 * RCC
 * ================================================================ */
#define RCC_BASE        0x40023800UL
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x34))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))
/* DCKCFGR2 offset 0x90 confirmed correct on STM32F72x by hardware probing */
#define RCC_DCKCFGR2    (*(volatile uint32_t *)(RCC_BASE + 0x90))

/* ================================================================
 * CRS (APB1 @ 0x40006C00) — trim HSI48 to USB SOF
 * ================================================================ */
#define CRS_BASE        0x40006C00UL
#define CRS_CR          (*(volatile uint32_t *)(CRS_BASE + 0x00))
#define CRS_CFGR        (*(volatile uint32_t *)(CRS_BASE + 0x04))

/* ================================================================
 * GPIOA — PA11 = OTG_FS_DM, PA12 = OTG_FS_DP (AF10)
 * ================================================================ */
#define GPIOA_BASE      0x40020000UL
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OSPEEDR   (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_AFRH      (*(volatile uint32_t *)(GPIOA_BASE + 0x24))

/* ================================================================
 * GPIOE — debug LEDs
 * ================================================================ */
#define GPIOE_ODR       (*(volatile uint32_t *)(0x40021000UL + 0x14))
#define LED2            (1u << 0)   /* PE0 — BLUE  */
#define LED3            (1u << 1)   /* PE1 — GREEN */

/* ================================================================
 * OTG_FS peripheral (@ 0x50000000) — core global registers
 * ================================================================ */
#define OTG_BASE        0x50000000UL

#define GOTGCTL         (*(volatile uint32_t *)(OTG_BASE + 0x000))
#define GAHBCFG         (*(volatile uint32_t *)(OTG_BASE + 0x008))
#define GUSBCFG         (*(volatile uint32_t *)(OTG_BASE + 0x00C))
#define GRSTCTL         (*(volatile uint32_t *)(OTG_BASE + 0x010))
#define GINTSTS         (*(volatile uint32_t *)(OTG_BASE + 0x014))
#define GINTMSK         (*(volatile uint32_t *)(OTG_BASE + 0x018))
#define GRXFSIZ         (*(volatile uint32_t *)(OTG_BASE + 0x024))
#define GNPTXFSIZ       (*(volatile uint32_t *)(OTG_BASE + 0x028))
#define GCCFG           (*(volatile uint32_t *)(OTG_BASE + 0x038))
#define PCGCCTL         (*(volatile uint32_t *)(OTG_BASE + 0xE00))

/* ================================================================
 * OTG_FS host-mode registers
 * ================================================================ */
#define HCFG            (*(volatile uint32_t *)(OTG_BASE + 0x400))
#define HFIR            (*(volatile uint32_t *)(OTG_BASE + 0x404))

/*
 * HPRT — Host Port Control and Status (0x440)
 * Special register: some bits are W1C.  Always use hprt_write() to
 * avoid accidentally clearing PCDET(1), PENA(2), PENCHNG(3), POCCHNG(5).
 */
#define HPRT_REG        (*(volatile uint32_t *)(OTG_BASE + 0x440))
/* Mask of W1C bits that must NOT be written as 1 unless intentionally clearing */
#define HPRT_W1C_MASK   ((1u<<1)|(1u<<2)|(1u<<3)|(1u<<5))

static inline void hprt_write(uint32_t set_bits)
{
    /* Read current value, clear all W1C bits, then OR in the desired bits */
    uint32_t v = HPRT_REG & ~HPRT_W1C_MASK;
    HPRT_REG = v | set_bits;
}

/* ================================================================
 * Module state
 * ================================================================ */
static volatile int g_connected = 0;   /* 1 when a device is on the port */

/* ================================================================
 * Helpers
 * ================================================================ */
static void udelay(volatile uint32_t n) { while (n--) __asm__("nop"); }

/* ================================================================
 * usb_host_init
 * ================================================================ */
void usb_host_init(void)
{
    /* 1. HSI48 + CRS ---------------------------------------------------- */
    RCC_CR |= (1u << 26);               /* HSI48ON */
    while (!(RCC_CR & (1u << 27))) {}   /* wait HSI48RDY */

    /* CLK48SEL[27:26] = 11 → select HSI48 as 48 MHz source */
    RCC_DCKCFGR2 = (RCC_DCKCFGR2 & ~(3u << 26)) | (3u << 26);

    RCC_APB1ENR |= (1u << 27);          /* CRSEN */

    /* CRS: trim HSI48 to USB SOF (1 kHz)
     *   RELOAD = 0xBB7F = 48 000 000/1000 - 1
     *   FELIM  = 22     (≈ 1% tolerance)
     *   SYNCSRC[29:28] = 10 → USB SOF */
    CRS_CFGR = 0xBB7Fu | (22u << 16) | (2u << 28);
    CRS_CR  |= (1u << 6) | (1u << 5);  /* AUTOTRIMEN | CEN */

    /* 2. GPIO: PA11/PA12 as OTG_FS_DM/DP, AF10 -------------------------- */
    RCC_AHB1ENR |= (1u << 0);          /* GPIOAEN */

    /* AF mode: PA11[23:22]=10, PA12[25:24]=10 */
    GPIOA_MODER   = (GPIOA_MODER   & ~((3u<<22)|(3u<<24))) | ((2u<<22)|(2u<<24));
    /* Very high speed */
    GPIOA_OSPEEDR |= (3u<<22)|(3u<<24);
    /* AFRH: AF10=0xA at PA11[15:12] and PA12[19:16] */
    GPIOA_AFRH    = (GPIOA_AFRH    & ~((0xFu<<12)|(0xFu<<16))) | ((0xAu<<12)|(0xAu<<16));

    /* 3. Enable OTG_FS clock (AHB2ENR bit 7) ----------------------------- */
    RCC_AHB2ENR |= (1u << 7);
    udelay(1000);

    /* 4. Core soft reset ------------------------------------------------- */
    uint32_t timeout;
    timeout = 200000;
    while (!(GRSTCTL & (1u << 31)) && --timeout) {}  /* wait AHBIDL */
    GRSTCTL |= (1u << 0);                              /* CSRST */
    timeout = 200000;
    while ((GRSTCTL & (1u << 0)) && --timeout) {}
    udelay(30);

    /* 5. Power up transceiver -------------------------------------------- */
    GCCFG = (1u << 16);    /* PWRDWN=1 (active) */
    PCGCCTL = 0;            /* ensure clocks not gated */

    /* 6. Force host mode: GUSBCFG.FHMOD (bit 29) ------------------------- */
    GUSBCFG = (GUSBCFG & ~(1u << 30)) | (1u << 29);  /* clear FDMOD, set FHMOD */
    /* Poll GINTSTS.CMOD (bit 0) until = 1 (host mode), ~200 ms timeout */
    timeout = 3200000;
    while (!(GINTSTS & (1u << 0)) && --timeout) {}
    if (!(GINTSTS & (1u << 0))) {
        /* Mode switch failed — blink LED3 */
        while (1) {
            for (int i = 0; i < 5; i++) {
                GPIOE_ODR |=  LED3; udelay(80000);
                GPIOE_ODR &= ~LED3; udelay(80000);
            }
            udelay(400000);
        }
    }

    /* 7. Host config: FSLSPCS[1:0]=01 (48 MHz clock for FS/LS) ----------- */
    HCFG = (HCFG & ~(3u << 0)) | (1u << 0);

    /* Frame interval: 48 000 for 1 ms SOF @ 48 MHz */
    HFIR = 48000u;

    /* 8. FIFO sizes (words):
     *   RX FIFO:  128 words (512 B)
     *   NP TX:     96 words (384 B), starts at 128              */
    GRXFSIZ   = 128u;
    GNPTXFSIZ = (96u << 16) | 128u;

    /* 9. Flush FIFOs ----------------------------------------------------- */
    GRSTCTL |= (0x10u << 6) | (1u << 5);   /* TXFNUM=all, TXFFLSH */
    timeout = 200000;
    while ((GRSTCTL & (1u << 5)) && --timeout) {}
    GRSTCTL |= (1u << 4);                   /* RXFFLSH */
    timeout = 200000;
    while ((GRSTCTL & (1u << 4)) && --timeout) {}

    /* 10. Turn on port power --------------------------------------------- */
    hprt_write(1u << 12);   /* PPWR=1 */

    /* 11. Enable connect-detect interrupt --------------------------------- */
    GINTSTS = 0xFFFFFFFFu;
    GINTMSK = (1u << 24);   /* HPRTINT: host port interrupt */
    GAHBCFG |= (1u << 0);   /* GINT */
}

/* ================================================================
 * usb_host_poll — call from main loop; updates LED2 + g_connected
 * ================================================================ */
void usb_host_poll(void)
{
    /* Check HPRT.PCSTS (bit 0) directly — most reliable indicator */
    if (HPRT_REG & (1u << 0)) {
        g_connected = 1;
        GPIOE_ODR |= LED2;
    } else {
        g_connected = 0;
        GPIOE_ODR &= ~LED2;
    }

    /* Service host-port interrupt if pending */
    if (!(GINTSTS & (1u << 24))) return;

    uint32_t hprt = HPRT_REG;

    /* Port connect detected: W1C bit 1 */
    if (hprt & (1u << 1)) {
        /* Clear PCDET by writing 1 to it (keep other W1C bits as 0) */
        HPRT_REG = (hprt & ~HPRT_W1C_MASK) | (1u << 1);
    }

    /* Port enable changed: W1C bit 3 */
    if (hprt & (1u << 3)) {
        HPRT_REG = (hprt & ~HPRT_W1C_MASK) | (1u << 3);
    }
}

/* ================================================================
 * usb_host_connected
 * ================================================================ */
int usb_host_connected(void)
{
    return g_connected;
}
