/*
 * usb_device.c — USB-C device bring-up on STM32F723
 *
 * Uses OTG_HS peripheral in full-speed device mode (base 0x40040000).
 * PB14 = OTG_HS_DM, PB15 = OTG_HS_DP (AF12).
 *
 * Clock: HSI48 as CLK48 source (set up by usb_host_init / sdcard_init).
 *
 * Bring-up goal: detect USB bus reset from the host PC (GINTSTS.USBRST),
 * confirming the D+/D- lines are connected through the USB-C data connector.
 * LED3 (PE1) lights on first bus reset received.
 */

#include <stdint.h>
#include "usb_device.h"

/* ================================================================
 * RCC
 * ================================================================ */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))

/* ================================================================
 * GPIOB — PB14 = OTG_HS_DM, PB15 = OTG_HS_DP (AF12)
 * ================================================================ */
#define GPIOB_BASE      0x40020400UL
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OSPEEDR   (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_AFRH      (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

/* ================================================================
 * GPIOE — LED3 (PE1, GREEN)
 * ================================================================ */
#define GPIOE_ODR       (*(volatile uint32_t *)(0x40021000UL + 0x14))
#define LED3            (1u << 1)

/* ================================================================
 * OTG_HS core global registers (base 0x40040000)
 * ================================================================ */
#define OTG_HS_BASE     0x40040000UL

#define HS_GAHBCFG      (*(volatile uint32_t *)(OTG_HS_BASE + 0x008))
#define HS_GUSBCFG      (*(volatile uint32_t *)(OTG_HS_BASE + 0x00C))
#define HS_GRSTCTL      (*(volatile uint32_t *)(OTG_HS_BASE + 0x010))
#define HS_GINTSTS      (*(volatile uint32_t *)(OTG_HS_BASE + 0x014))
#define HS_GINTMSK      (*(volatile uint32_t *)(OTG_HS_BASE + 0x018))
#define HS_GRXFSIZ      (*(volatile uint32_t *)(OTG_HS_BASE + 0x024))
#define HS_GNPTXFSIZ    (*(volatile uint32_t *)(OTG_HS_BASE + 0x028))
#define HS_GCCFG        (*(volatile uint32_t *)(OTG_HS_BASE + 0x038))
#define HS_PCGCCTL      (*(volatile uint32_t *)(OTG_HS_BASE + 0xE00))

/* ================================================================
 * OTG_HS device-mode registers
 * ================================================================ */
#define HS_DCFG         (*(volatile uint32_t *)(OTG_HS_BASE + 0x800))
#define HS_DCTL         (*(volatile uint32_t *)(OTG_HS_BASE + 0x804))
#define HS_DSTS         (*(volatile uint32_t *)(OTG_HS_BASE + 0x808))

/* ================================================================
 * Module state
 * ================================================================ */
static volatile int g_connected = 0;

/* ================================================================
 * Helpers
 * ================================================================ */
static void udelay(volatile uint32_t n) { while (n--) __asm__("nop"); }

/* ================================================================
 * usb_device_init
 * ================================================================ */
void usb_device_init(void)
{
    /* 1. Enable GPIOB clock ------------------------------------------- */
    RCC_AHB1ENR |= (1u << 1);   /* GPIOBEN */

    /* 2. PB14/PB15 as OTG_HS_DM/DP, AF12, very high speed ------------ */
    /* MODER: PB14[29:28]=10, PB15[31:30]=10 */
    GPIOB_MODER   = (GPIOB_MODER   & ~((3u<<28)|(3u<<30))) | ((2u<<28)|(2u<<30));
    GPIOB_OSPEEDR |= (3u<<28)|(3u<<30);
    /* AFRH: AF12=0xC at PB14[27:24] and PB15[31:28] */
    GPIOB_AFRH    = (GPIOB_AFRH    & ~((0xFu<<24)|(0xFu<<28))) | ((0xCu<<24)|(0xCu<<28));

    /* 3. Enable OTG_HS clock (AHB1ENR bit 29) ------------------------- */
    RCC_AHB1ENR |= (1u << 29);  /* OTGHSEN */
    udelay(1000);

    /* 4. Soft-disconnect while initialising --------------------------- */
    HS_DCTL |= (1u << 1);   /* SDIS=1 */

    /* 5. Power up PHY, force device mode.
     * FDMOD (bit 30): force device mode — belt-and-suspenders since
     * the floating ID pin already gives CIDSTS=1 (B-device).
     * PHYSEL (bit 6): internal FS serial transceiver.
     * TRDT[13:10] = 13 (0xD): correct for 16 MHz AHB (HSI16, no PLL). */
    HS_GCCFG   = (1u << 16);    /* PWRDWN=1 */
    HS_PCGCCTL = 0;
    HS_GUSBCFG = (HS_GUSBCFG & ~((1u<<29)|(0xFu<<10))) /* clear FHMOD, TRDT */
               | (1u << 30)                              /* FDMOD */
               | (1u << 6)                               /* PHYSEL */
               | (13u << 10);                            /* TRDT=13 */
    udelay(320000);  /* 20 ms — let PHY and mode switch settle */

    /* 6. Device config: full-speed internal PHY ----------------------- */
    HS_DCFG = (3u << 0)   /* DSPD=11: full speed, internal PHY */
            | (1u << 2);  /* NZLSOHSK: STALL non-zero-length status OUT */

    /* 7. FIFO sizes (words):
     *   RX:     128 words (512 B)
     *   EP0 TX:  64 words (256 B), starts at word 128 */
    HS_GRXFSIZ   = 128u;
    HS_GNPTXFSIZ = (64u << 16) | 128u;

    /* 8. Flush FIFOs -------------------------------------------------- */
    uint32_t timeout;
    HS_GRSTCTL |= (0x10u << 6) | (1u << 5);  /* TXFNUM=all, TXFFLSH */
    timeout = 200000;
    while ((HS_GRSTCTL & (1u << 5)) && --timeout) {}
    HS_GRSTCTL |= (1u << 4);                  /* RXFFLSH */
    timeout = 200000;
    while ((HS_GRSTCTL & (1u << 4)) && --timeout) {}

    /* 9. Enable USB reset and enumeration-done interrupts ------------- */
    HS_GINTSTS = 0xFFFFFFFFu;
    HS_GINTMSK = (1u << 12)   /* USBRST  — host issued bus reset */
               | (1u << 13);  /* ENUMDNE — enumeration speed detected */
    HS_GAHBCFG |= (1u << 0);  /* GINT */

    /* 10. Connect: release soft-disconnect ---------------------------- */
    HS_DCTL &= ~(1u << 1);  /* SDIS=0 — enables D+ pull-up */
}

/* ================================================================
 * usb_device_poll — call from main loop
 * ================================================================ */
void usb_device_poll(void)
{
    uint32_t sts = HS_GINTSTS;

    if (sts & (1u << 12)) {
        /* USBRST: host issued a bus reset — D+/D- are live */
        g_connected = 1;
        GPIOE_ODR |= LED3;
        HS_GINTSTS = (1u << 12);   /* W1C */
    }

    if (sts & (1u << 13)) {
        /* ENUMDNE: enumeration speed detected */
        HS_GINTSTS = (1u << 13);   /* W1C */
    }
}

/* ================================================================
 * usb_device_connected
 * ================================================================ */
int usb_device_connected(void)
{
    return g_connected;
}
