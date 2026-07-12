/*
 * usb_host.c — USB-A host bring-up on STM32F723
 *
 * Uses OTG_FS peripheral in host mode (base 0x50000000).
 * PA11 = OTG_FS_DM, PA12 = OTG_FS_DP (AF10).
 * VBUS is hardwired from USB-C 5V input — no GPIO switch needed.
 *
 * Clock: PLL48CLK (PLLQ = 48 MHz) as CLK48 source.
 * On STM32F72x only bit 27 of RCC_DCKCFGR2 is writable, so
 * CLK48SEL[27:26] = 10 (PLLQ) is what actually gets selected.
 * HSI48 is not accessible via the CLK48SEL mux on this device.
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
#define RCC_PLLCFGR     (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x34))
#define RCC_AHB2RSTR    (*(volatile uint32_t *)(RCC_BASE + 0x14))
/* DCKCFGR2 offset 0x90 confirmed correct on STM32F72x by hardware probing */
#define RCC_DCKCFGR2    (*(volatile uint32_t *)(RCC_BASE + 0x90))

/* ================================================================
 * GPIOA — PA11 = OTG_FS_DM, PA12 = OTG_FS_DP (AF10)
 * ================================================================ */
#define GPIOA_BASE      0x40020000UL
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OSPEEDR   (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_PUPDR     (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
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
    /* 1. Start PLL so PLLQ = 48 MHz — required for CLK48 on STM32F72x.
     *    On F72x, CLK48SEL in DCKCFGR2 only has bit 27 writable, giving
     *    CLK48SEL=10 (PLLQ).  SYSCLK stays on HSI16; we only need PLLQ.
     *    PLLM=8, PLLN=96, PLLQ=4 → VCO=192 MHz, PLLQ=48 MHz. */
    if (!(RCC_CR & (1u << 25))) {       /* skip if PLLRDY already set */
        RCC_PLLCFGR = (4u  << 24)       /* PLLQ=4  → 48 MHz          */
                    | (0u  << 22)        /* PLLSRC=HSI16               */
                    | (1u  << 16)        /* PLLP=÷4 (unused)           */
                    | (96u <<  6)        /* PLLN=96 → VCO=192 MHz      */
                    |  8u;               /* PLLM=8  → VCO_in=2 MHz     */
        RCC_CR |= (1u << 24);           /* PLLON */
        while (!(RCC_CR & (1u << 25))) {} /* wait PLLRDY */
    }
    /* CLK48SEL: write both bits; only bit 27 sticks on F72x → PLLQ selected */
    RCC_DCKCFGR2 = (RCC_DCKCFGR2 & ~(3u << 26)) | (1u << 27);

    /* 2. GPIO ------------------------------------------------------------ */
    RCC_AHB1ENR |= (1u << 0);          /* GPIOAEN */

    /* PA10 = OTG_FS_ID as AF10 with internal pull-down.
     * The pad is damaged; the internal 45k pull-down may still reach the pin
     * and hold CIDSTS=0 (A-device).  v0.2 fix: 10k pull-down on PA10. */
    GPIOA_MODER   = (GPIOA_MODER  & ~(3u<<20)) | (2u<<20);   /* AF mode */
    GPIOA_PUPDR   = (GPIOA_PUPDR  & ~(3u<<20)) | (2u<<20);   /* pull-down */
    GPIOA_AFRH    = (GPIOA_AFRH   & ~(0xFu<<8)) | (0xAu<<8); /* AF10 */

    /* PA11/PA12 as OTG_FS_DM/DP, AF10 */
    /* AF mode: PA11[23:22]=10, PA12[25:24]=10 */
    GPIOA_MODER   = (GPIOA_MODER   & ~((3u<<22)|(3u<<24))) | ((2u<<22)|(2u<<24));
    /* Very high speed */
    GPIOA_OSPEEDR |= (3u<<22)|(3u<<24);
    /* AFRH: AF10=0xA at PA11[15:12] and PA12[19:16] */
    GPIOA_AFRH    = (GPIOA_AFRH    & ~((0xFu<<12)|(0xFu<<16))) | ((0xAu<<12)|(0xAu<<16));

    /* 3. Reset then enable OTG_FS peripheral so it comes up clean.
     * The OTG core latches CIDSTS (ID pin) the moment it first powers up.
     * We must have FHMOD set and PA10 grounded BEFORE the core samples the
     * ID pin, which means asserting FHMOD immediately after clock enable —
     * before GCCFG.PWRDWN is touched. */
    RCC_AHB2RSTR |=  (1u << 7);   /* assert OTG_FS reset */
    udelay(1000);
    RCC_AHB2RSTR &= ~(1u << 7);   /* release reset */
    RCC_AHB2ENR  |=  (1u << 7);   /* enable OTG_FS clock */

    /* 4. Set FHMOD (force host mode) IMMEDIATELY — before GCCFG.PWRDWN
     * powers the transceiver and the PHY samples the ID pin. */
    GUSBCFG = (1u << 29)   /* FHMOD: force host mode */
            | (1u << 6);    /* PHYSEL: internal FS transceiver */

    /* A-device/VBUS valid overrides — belt-and-suspenders in case the
     * OTG state machine still checks session validity. */
    GOTGCTL |= (1u<<2)|(1u<<3)|(1u<<4)|(1u<<5);

    /* 5. Power up PHY and ungate clocks ---------------------------------- */
    GCCFG   = (1u << 16);  /* PWRDWN=1: enable transceiver */
    PCGCCTL = 0;

    /* 6. Wait ≥25 ms for mode switch (Synopsys spec).
     * Skip CSRST — it requires the 48 MHz PHY clock which is not present
     * on OTG_FS; CSRST hangs and locks the entire core. FHMOD alone is
     * sufficient to switch modes after the RCC reset above. */
    uint32_t timeout;
    udelay(800000);   /* ~50 ms */

    /* 8. Host config: FSLSPCS[1:0]=01 (48 MHz clock for FS/LS) ----------- */
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

    /* 10. Turn on port power — wait for AHB idle first, then write PPWR.
     * The HPRT register ignores writes if the host port isn't fully ready. */
    udelay(25000);  /* ~1.5 ms at HSI16 */
    hprt_write(1u << 12);   /* PPWR=1 */
    udelay(25000);  /* let PPWR settle */

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
    /* Ensure port power stays on (re-apply if lost) */
    if (!(HPRT_REG & (1u << 12)))
        hprt_write(1u << 12);

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
