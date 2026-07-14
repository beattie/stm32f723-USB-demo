/*
 * usb_device.c — USB-C device bring-up on STM32F723 using OTGPHYC
 *
 * Uses OTG_HS peripheral with the on-chip OTGPHYC embedded HS PHY.
 * PB14 (USB_HS_DM) and PB15 (USB_HS_DP) are driven as dedicated analog
 * pads by OTGPHYC — no GPIO AF configuration required.
 *
 * Clock: OTGPHYC PLL uses HSI16 (16 MHz) directly as reference (PLLsel=3).
 * No PLLQ dependency; PLLQ only needed by usb_host.c for OTG_FS.
 *
 * Bring-up goal: detect USB bus reset from the host PC (GINTSTS.USBRST),
 * confirming the D+/D- lines are live through the USB-C connector.
 * LED3 (PE1) lights on first bus reset received.
 *
 * Why OTGPHYC instead of PHYSEL=1 (internal FS serial transceiver):
 *   PHYSEL=1 path ties D+ pull-up to the VBUS comparator (BSVLD). With PB13
 *   NC on v0.1, BSVLD stays 0 and the pull-up never activates regardless of
 *   software overrides. OTGPHYC bypasses the VBUS comparator entirely.
 */

#include <stdint.h>
#include "usb_device.h"

/* ================================================================
 * RCC
 * ================================================================ */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1RSTR    (*(volatile uint32_t *)(RCC_BASE + 0x10))
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB2RSTR    (*(volatile uint32_t *)(RCC_BASE + 0x24))
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x44))

/* ================================================================
 * GPIOE — LED3 (PE1, GREEN)
 * ================================================================ */
#define GPIOE_ODR       (*(volatile uint32_t *)(0x40021000UL + 0x14))
#define LED3            (1u << 1)

/* ================================================================
 * OTGPHYC — on-chip embedded HS PHY (base 0x40017C00)
 *
 * PLL  (offset 0x000):
 *   bit  0   = PLLEN (enable PLL)
 *   bits[3:1]= PLLsel:
 *              0x0 = 12 MHz, 0x2 = 12.5 MHz, 0x6 = 16 MHz,
 *              0x8 = 24 MHz, 0xC = 25 MHz
 * TUNE (offset 0x00C): PHY tuning; set to 0x00000F13
 * LDO  (offset 0x018):
 *   bit  1   = LDO_STATUS  (read: 1 = LDO ready)
 *   bit  2   = LDO_ENABLE  (write 1 to enable)
 * ================================================================ */
#define PHYC_BASE       0x40017C00UL
#define PHYC_PLL        (*(volatile uint32_t *)(PHYC_BASE + 0x000))
#define PHYC_TUNE       (*(volatile uint32_t *)(PHYC_BASE + 0x00C))
#define PHYC_LDO        (*(volatile uint32_t *)(PHYC_BASE + 0x018))

#define PHYC_PLLEN          (1u << 0)
#define PHYC_PLLSEL_16MHZ   (0x6u)   /* bits[3:1]=011 i.e. (0x3 << 1) */
#define PHYC_LDO_STATUS     (1u << 1)
#define PHYC_LDO_ENABLE     (1u << 2)
#define PHYC_TUNE_VALUE     0x00000F13U

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

/* OTG_HS device-mode registers */
#define HS_DCFG         (*(volatile uint32_t *)(OTG_HS_BASE + 0x800))
#define HS_DCTL         (*(volatile uint32_t *)(OTG_HS_BASE + 0x804))

/* ================================================================
 * Module state
 * ================================================================ */
static volatile int g_connected = 0;

static void udelay(volatile uint32_t n) { while (n--) __asm__("nop"); }

/* ================================================================
 * usb_device_init
 * ================================================================ */
void usb_device_init(void)
{
    uint32_t timeout;

    /* 1. Enable peripheral clocks:
     *   APB2ENR bit 31 = OTGPHYCEN  — OTGPHYC peripheral
     *   AHB1ENR bit 29 = OTGHSEN    — OTG_HS core
     *   AHB1ENR bit 30 = OTGHSULPIEN — required for embedded PHY path */
    RCC_APB2ENR |= (1u << 31);
    RCC_AHB1ENR |= (1u << 29) | (1u << 30);
    udelay(1000);

    /* 2. Reset and release OTG_HS core */
    RCC_AHB1RSTR |=  (1u << 29);
    udelay(100);
    RCC_AHB1RSTR &= ~(1u << 29);
    udelay(1000);

    /* 3. Select embedded UTMI+ PHY in OTG_HS core (before PHYC init):
     *   Clear PHYSEL (bit 6) — not internal FS serial transceiver
     *   Clear ULPI_UTMI_SEL (bit 4) — UTMI+ not ULPI
     *   Clear TSDPS (bit 22), ULPIFSLS (bit 17) — ULPI-related, irrelevant
     *   Clear ULPIEVBUSD (bit 20), ULPIEVBUSI (bit 21) — VBUS drive via ULPI */
    HS_GUSBCFG &= ~((1u << 6) | (1u << 4) | (1u << 22) | (1u << 17)
                  | (1u << 20) | (1u << 21));

    /* 4. GCCFG: disable internal FS transceiver; enable HS PHY path
     *   Clear PWRDWN (bit 16) — power down internal FS transceiver
     *   Set   PHYHSEN (bit 23) — enable HS PHY */
    HS_GCCFG = (HS_GCCFG & ~(1u << 16)) | (1u << 23);

    /* 5. OTGPHYC: enable LDO, wait for it to stabilise */
    PHYC_LDO |= PHYC_LDO_ENABLE;
    timeout = 200000;
    while (!(PHYC_LDO & PHYC_LDO_STATUS) && --timeout) {}

    /* 6. OTGPHYC: set PLL reference = 16 MHz (HSI16), load tuning, start PLL */
    PHYC_PLL   = PHYC_PLLSEL_16MHZ;    /* PLLsel=3, PLLEN still 0 */
    PHYC_TUNE |= PHYC_TUNE_VALUE;
    PHYC_PLL  |= PHYC_PLLEN;           /* enable PLL */
    udelay(32000);                      /* ≥2 ms for PHY PLL to lock */

    /* 7. Ungate PHY clocks */
    HS_PCGCCTL = 0;

    /* 8. Force device mode; set turnaround time (TRDT=9 for HS).
     *   No CSRST: the hardware reset via AHB1RSTR already cleared the core.
     *   CSRST requires an active PHY clock to complete — if issued too early
     *   it stalls indefinitely. Skip it. */
    HS_GUSBCFG = (HS_GUSBCFG
                  & ~((0xFu << 10) | (1u << 29)))   /* clear TRDT, clear FHMOD */
               | (1u << 30)                          /* FDMOD: force device mode */
               | (9u << 10);                         /* TRDT=9 */

    /* Wait for mode switch — RM says up to 25 ms; give 200 ms margin */
    timeout = 3200000;   /* ~200 ms at HSI16 */
    while ((HS_GINTSTS & (1u << 0)) && --timeout) {}  /* wait CMOD=0 */

    /* 10. Soft-disconnect while finishing configuration */
    HS_DCTL |= (1u << 1);   /* SDIS=1 */

    /* 11. Device config: high speed, NAK on ZLS out */
    HS_DCFG = (0u << 0)    /* DSPD=00: high speed */
            | (1u << 2);   /* NZLSOHSK */

    /* 12. FIFO sizes (in 32-bit words):
     *   RX FIFO:  256 words (1024 B) — shared, starts at 0
     *   EP0 TX:    64 words ( 256 B) — starts at 256            */
    HS_GRXFSIZ   = 256u;
    HS_GNPTXFSIZ = (64u << 16) | 256u;

    /* 13. Enable USB reset and enumeration-done interrupts */
    HS_GINTSTS = 0xFFFFFFFFu;
    HS_GINTMSK = (1u << 12)   /* USBRST  — host issued bus reset */
               | (1u << 13);  /* ENUMDNE — enumeration speed detected */
    HS_GAHBCFG |= (1u << 0);  /* GINT: enable interrupt output */

    /* 14. Connect: release soft-disconnect, enabling D+ pull-up */
    HS_DCTL &= ~(1u << 1);  /* SDIS=0 */
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
