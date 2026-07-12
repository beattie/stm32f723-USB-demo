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
 * OTG_HS internal FS transceiver uses PLL48CLK (PLLQ), NOT the
 * CLK48SEL mux (which feeds only OTG_FS / SDMMC / RNG).
 * We run SYSCLK on HSI16 but spin up PLL just to produce PLLQ=48 MHz.
 *   HSI16 → /PLLM=8 → 2 MHz → ×PLLN=96 → 192 MHz VCO → /PLLQ=4 → 48 MHz
 * ================================================================ */
#define RCC_BASE        0x40023800UL
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR     (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_AHB1RSTR    (*(volatile uint32_t *)(RCC_BASE + 0x10))

/* ================================================================
 * GPIOB — PB14 = OTG_HS_DM, PB15 = OTG_HS_DP (AF12)
 * ================================================================ */
#define GPIOB_BASE      0x40020400UL
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OSPEEDR   (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_BSRR      (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
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

#define HS_GOTGCTL      (*(volatile uint32_t *)(OTG_HS_BASE + 0x000))
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
    /* 0. Start PLL so PLLQ = 48 MHz (required by OTG_HS internal FS PHY).
     *    PLL must be off to write PLLCFGR; at reset it is off.
     *    PLLM=8, PLLN=96, PLLP=÷4 (unused), PLLSRC=HSI16, PLLQ=4.
     *    SYSCLK stays on HSI16 — we only need PLLQ. */
    if (!(RCC_CR & (1u << 25))) {   /* if PLLRDY=0, PLL not yet running */
        RCC_PLLCFGR = (4u  << 24)  /* PLLQ=4  → 192/4 = 48 MHz        */
                    | (0u  << 22)  /* PLLSRC=HSI16                     */
                    | (1u  << 16)  /* PLLP=01 → ÷4 (not used for SYSCLK)*/
                    | (96u <<  6)  /* PLLN=96 → VCO=192 MHz            */
                    |  8u;         /* PLLM=8  → VCO_in=2 MHz           */
        RCC_CR |= (1u << 24);      /* PLLON */
        while (!(RCC_CR & (1u << 25))) {}  /* wait PLLRDY */
    }

    /* 1. Enable GPIOB clock ------------------------------------------- */
    RCC_AHB1ENR |= (1u << 1);   /* GPIOBEN */

    /* 2. PB13 HIGH — simulates VBUS present to the OTG_HS VBUS comparator.
     *    PB13 is NC on this board; driving it from GPIO fools the analog
     *    comparator so the PHY enables its clock and the D+ pull-up path. */
    GPIOB_MODER = (GPIOB_MODER & ~(3u<<26)) | (1u<<26);  /* PB13 output */
    GPIOB_BSRR  = (1u << 13);                              /* PB13 = HIGH */

    /* PB14/PB15 as OTG_HS_DM/DP, AF12, very high speed ------------ */
    /* MODER: PB14[29:28]=10, PB15[31:30]=10 */
    GPIOB_MODER   = (GPIOB_MODER   & ~((3u<<28)|(3u<<30))) | ((2u<<28)|(2u<<30));
    GPIOB_OSPEEDR |= (3u<<28)|(3u<<30);
    /* AFRH: AF12=0xC at PB14[27:24] and PB15[31:28] */
    GPIOB_AFRH    = (GPIOB_AFRH    & ~((0xFu<<24)|(0xFu<<28))) | ((0xCu<<24)|(0xCu<<28));

    /* 3. RCC peripheral reset then enable OTG_HS clock.
     * Bit 29 = OTGHSEN/OTGHSRST.  Do NOT enable OTGHSULPIEN (bit 30):
     * that is the ULPI clock for an external HS PHY — enabling it without
     * a real ULPI PHY stalls the core waiting for a ULPI handshake. */
    RCC_AHB1RSTR |=  (1u << 29);   /* assert OTG_HS reset */
    udelay(1000);
    RCC_AHB1RSTR &= ~(1u << 29);   /* release reset */
    RCC_AHB1ENR  |=  (1u << 29);   /* enable OTG_HS clock */
    udelay(1000);

    /* 4. GUSBCFG and GCCFG must be set BEFORE CSRST so the core comes
     * out of soft-reset already knowing the PHY type and mode.
     * FDMOD (bit 30): force device mode.
     * PHYSEL (bit 6): internal FS serial transceiver (PLLQ = 48 MHz).
     * TRDT[13:10] = 13: adequate for 16 MHz AHB. */
    HS_GUSBCFG  = (1u << 30)    /* FDMOD */
                | (1u << 6)     /* PHYSEL */
                | (13u << 10);  /* TRDT=13 */
    /* PWRDWN=1: power up internal FS PHY.
     * VBDEN=1: enable VBUS detection on PB13 — PB13 is driven HIGH by GPIO
     * above to simulate VBUS present, which the PHY needs to clock up. */
    HS_GCCFG    = (1u << 16) | (1u << 21);
    HS_PCGCCTL  = 0;             /* ungate clocks */
    udelay(50000);               /* ≥3 ms for PHY to stabilise */

    /* 5. Soft-disconnect while finishing configuration ---------------- */
    HS_DCTL |= (1u << 1);   /* SDIS=1 */

    /* Force session valid — enables D+ pull-up without VBUS pin sensing.
     * VBVALOEN/VBVALOVAL (bits 2,3): tell the analog PHY that VBUS is valid.
     * BVALOEN/BVALOVAL   (bits 6,7): tell the digital OTG SM session is valid. */
    HS_GOTGCTL |= (1u<<2)|(1u<<3)|(1u<<6)|(1u<<7);

    udelay(320000);  /* 20 ms */

    /* 6. Device config: full-speed internal PHY ----------------------- */
    HS_DCFG = (3u << 0)   /* DSPD=11: FS internal PHY */
            | (1u << 2);  /* NZLSOHSK */

    /* 7. FIFO sizes */
    HS_GRXFSIZ   = 128u;
    HS_GNPTXFSIZ = (64u << 16) | 128u;

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
