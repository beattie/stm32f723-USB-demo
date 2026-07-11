/*
 * usb_dev.c — USB-C device bring-up on STM32F723
 *
 * Uses OTG_HS peripheral in full-speed device mode with the internal FS PHY
 * (PB14=DM, PB15=DP). No external ULPI chip needed.
 *
 * Clock: HSI48 oscillator + CRS auto-trim to USB SOF gives a stable 48 MHz
 * USB clock without an external crystal.
 *
 * Implements the minimum needed to enumerate on a host:
 *   GET_DESCRIPTOR  → device, configuration, string[0]
 *   SET_ADDRESS     → applied after status ZLP
 *   SET_CONFIGURATION → acknowledged
 *
 * Verify enumeration with:   lsusb -d dead:f723 -v
 *                        or  dmesg | tail  (Linux)
 */

#include <stdint.h>
#include "usb_dev.h"

/* ================================================================
 * RCC — Reset and Clock Control
 * ================================================================ */
#define RCC_BASE        0x40023800UL
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))
/* DCKCFGR2 is at 0x90 on both STM32F72x and F74x — register addresses are fixed in silicon.
 * The absence of PLLSAI on F72x does not shift the register layout. */
#define RCC_DCKCFGR2    (*(volatile uint32_t *)(RCC_BASE + 0x90))

/* ================================================================
 * CRS — Clock Recovery System (APB1 @ 0x40006C00)
 * Continuously trims HSI48 using USB SOF frames as reference.
 * ================================================================ */
#define CRS_BASE        0x40006C00UL
#define CRS_CR          (*(volatile uint32_t *)(CRS_BASE + 0x00))
#define CRS_CFGR        (*(volatile uint32_t *)(CRS_BASE + 0x04))

/* ================================================================
 * GPIOB — PB14 = OTG_HS_DM, PB15 = OTG_HS_DP (AF12)
 * ================================================================ */
#define GPIOB_BASE      0x40020400UL
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OSPEEDR   (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_AFRH      (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

/* ================================================================
 * OTG_HS peripheral (@ 0x40040000)
 * ================================================================ */
#define OTG_BASE        0x40040000UL

/* Core global registers */
#define GOTGCTL         (*(volatile uint32_t *)(OTG_BASE + 0x000))
#define GAHBCFG         (*(volatile uint32_t *)(OTG_BASE + 0x008))
#define GUSBCFG         (*(volatile uint32_t *)(OTG_BASE + 0x00C))
#define GRSTCTL         (*(volatile uint32_t *)(OTG_BASE + 0x010))
#define GINTSTS         (*(volatile uint32_t *)(OTG_BASE + 0x014))
#define GINTMSK         (*(volatile uint32_t *)(OTG_BASE + 0x018))
#define GRXSTSP         (*(volatile uint32_t *)(OTG_BASE + 0x020))
#define GRXFSIZ         (*(volatile uint32_t *)(OTG_BASE + 0x024))
#define GNPTXFSIZ       (*(volatile uint32_t *)(OTG_BASE + 0x028))
#define GCCFG           (*(volatile uint32_t *)(OTG_BASE + 0x038))
#define PCGCCTL         (*(volatile uint32_t *)(OTG_BASE + 0xE00))

/* Device-mode registers */
#define DCFG            (*(volatile uint32_t *)(OTG_BASE + 0x800))
#define DCTL            (*(volatile uint32_t *)(OTG_BASE + 0x804))
#define DIEPMSK         (*(volatile uint32_t *)(OTG_BASE + 0x810))
#define DOEPMSK         (*(volatile uint32_t *)(OTG_BASE + 0x814))
#define DAINTMSK        (*(volatile uint32_t *)(OTG_BASE + 0x81C))

/* IN endpoint 0 */
#define DIEPCTL0        (*(volatile uint32_t *)(OTG_BASE + 0x900))
#define DIEPINT0        (*(volatile uint32_t *)(OTG_BASE + 0x908))
#define DIEPTSIZ0       (*(volatile uint32_t *)(OTG_BASE + 0x910))

/* OUT endpoint 0 */
#define DOEPCTL0        (*(volatile uint32_t *)(OTG_BASE + 0xB00))
#define DOEPINT0        (*(volatile uint32_t *)(OTG_BASE + 0xB08))
#define DOEPTSIZ0       (*(volatile uint32_t *)(OTG_BASE + 0xB10))

/* Endpoint 0 data FIFO — read/write here to access EP0 RX/TX */
#define DFIFO0          (*(volatile uint32_t *)(OTG_BASE + 0x1000))

/* ================================================================
 * USB descriptors
 * VID=0xDEAD, PID=0xF723 — bring-up placeholder, change for production
 * ================================================================ */

static const uint8_t dev_desc[] = {
    18,     /* bLength */
    0x01,   /* bDescriptorType: DEVICE */
    0x00, 0x02, /* bcdUSB: 2.00 */
    0x00,   /* bDeviceClass: defined by interface */
    0x00,   /* bDeviceSubClass */
    0x00,   /* bDeviceProtocol */
    64,     /* bMaxPacketSize0: 64 bytes (full-speed maximum) */
    0xAD, 0xDE, /* idVendor:  0xDEAD */
    0x23, 0xF7, /* idProduct: 0xF723 */
    0x00, 0x01, /* bcdDevice: 1.00 */
    0x00,   /* iManufacturer: none */
    0x00,   /* iProduct: none */
    0x00,   /* iSerialNumber: none */
    1,      /* bNumConfigurations */
};

/* Configuration descriptor + interface descriptor, concatenated.
 * wTotalLength = 18 (9 + 9). No extra endpoints for bring-up. */
static const uint8_t cfg_desc[] = {
    /* Configuration */
    9,      /* bLength */
    0x02,   /* bDescriptorType: CONFIGURATION */
    18, 0,  /* wTotalLength: 18 */
    1,      /* bNumInterfaces */
    1,      /* bConfigurationValue */
    0,      /* iConfiguration: none */
    0x80,   /* bmAttributes: bus-powered */
    50,     /* bMaxPower: 100 mA */
    /* Interface */
    9,      /* bLength */
    0x04,   /* bDescriptorType: INTERFACE */
    0,      /* bInterfaceNumber */
    0,      /* bAlternateSetting */
    0,      /* bNumEndpoints: EP0 only */
    0xFF,   /* bInterfaceClass: vendor-specific (placeholder) */
    0x00,   /* bInterfaceSubClass */
    0x00,   /* bInterfaceProtocol */
    0,      /* iInterface: none */
};

/* String descriptor 0: supported language IDs (English US = 0x0409) */
static const uint8_t str0_desc[] = { 4, 0x03, 0x09, 0x04 };

/* ================================================================
 * State
 * ================================================================ */

/* SET_ADDRESS deferred: must be applied after the ZLP status stage. */
static uint8_t pending_addr = 0;

/* ================================================================
 * FIFO helpers
 * ================================================================ */

/* Write len bytes into the EP0 TX FIFO.
 * Caller must configure DIEPTSIZ0 before calling. */
static void fifo_write(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; ) {
        uint32_t word = 0;
        uint8_t  n = (len - i < 4) ? (uint8_t)(len - i) : 4;
        for (uint8_t b = 0; b < n; b++)
            word |= (uint32_t)data[i + b] << (b * 8);
        DFIFO0 = word;
        i += n;
    }
}

/* Read 'words' 32-bit words from the RX FIFO into buf. */
static void fifo_read(uint8_t *buf, uint16_t words) {
    for (uint16_t i = 0; i < words; i++) {
        uint32_t w = DFIFO0;
        buf[i*4+0] = (uint8_t)(w);
        buf[i*4+1] = (uint8_t)(w >> 8);
        buf[i*4+2] = (uint8_t)(w >> 16);
        buf[i*4+3] = (uint8_t)(w >> 24);
    }
}

/* ================================================================
 * EP0 control helpers
 * ================================================================ */

/* Queue an IN response on EP0 — sends min(len, requested) bytes. */
static void ep0_in(const uint8_t *data, uint16_t len, uint16_t requested) {
    if (len > requested) len = requested;
    DIEPTSIZ0 = (1u << 19) | len;          /* PKTCNT=1, XFRSIZ=len */
    DIEPCTL0 |= (1u << 31) | (1u << 26);   /* EPENA | CNAK */
    fifo_write(data, len);
}

/* Send a zero-length packet on EP0 IN (status-stage ACK). */
static void ep0_zlp(void) {
    DIEPTSIZ0 = (1u << 19);                /* PKTCNT=1, XFRSIZ=0 */
    DIEPCTL0 |= (1u << 31) | (1u << 26);   /* EPENA | CNAK */
}

/* Re-arm EP0 OUT to accept the next SETUP packet.
 * STUPCNT=3 allows up to 3 back-to-back SETUP packets before interrupt. */
static void ep0_rearm(void) {
    DOEPTSIZ0 = (3u << 29) | 24u;          /* STUPCNT=3, XFRSIZ=24 (3×8) */
    DOEPCTL0 |= (1u << 31) | (1u << 26);   /* EPENA | CNAK */
}

/* ================================================================
 * Control request handler
 * ================================================================ */

static void handle_setup(const uint8_t *s) {
    uint8_t  bmRequestType = s[0];
    uint8_t  bRequest      = s[1];
    uint16_t wValue        = (uint16_t)(s[2] | ((uint16_t)s[3] << 8));
    uint16_t wLength       = (uint16_t)(s[6] | ((uint16_t)s[7] << 8));

    if ((bmRequestType & 0x60u) == 0x00u) {
        /* Standard request */
        switch (bRequest) {

        case 0x06: /* GET_DESCRIPTOR */
            switch (wValue >> 8) {
            case 0x01: ep0_in(dev_desc,  sizeof(dev_desc),  wLength); break;
            case 0x02: ep0_in(cfg_desc,  sizeof(cfg_desc),  wLength); break;
            case 0x03:
                if ((wValue & 0xFF) == 0)
                    ep0_in(str0_desc, sizeof(str0_desc), wLength);
                else
                    ep0_zlp();
                break;
            default:   ep0_zlp(); break;
            }
            break;

        case 0x05: /* SET_ADDRESS — apply address after ZLP completes */
            pending_addr = (uint8_t)(wValue & 0x7Fu);
            ep0_zlp();
            break;

        case 0x09: /* SET_CONFIGURATION */
            ep0_zlp();
            break;

        case 0x00: /* GET_STATUS */
            { uint8_t st[2] = {0, 0};
              ep0_in(st, 2, wLength); }
            break;

        default:
            ep0_zlp();
            break;
        }
    } else {
        ep0_zlp();
    }
}

/* ================================================================
 * USB bus reset
 * ================================================================ */

static void handle_reset(void) {
    DCFG     &= ~(0x7Fu << 4);              /* device address = 0 */
    pending_addr = 0;
    DIEPMSK   = (1u << 0);                  /* XFRCM: IN transfer complete */
    DOEPMSK   = (1u << 3);                  /* STUPCMP: SETUP phase done */
    DAINTMSK  = (1u << 0) | (1u << 16);    /* EP0 IN + EP0 OUT */
    ep0_rearm();
}

/* ================================================================
 * Debug LEDs (GPIOE, temporary for bring-up)
 * LED stage encoding:
 *   LED2 on              = entered usb_dev_init
 *   LED2+LED3 on         = clocks + GPIO done, about to touch OTG_HS registers
 *   LED2+LED3+LED4 on    = past AHBIDL+reset, about to flush FIFOs
 *   LED2+LED4 on         = FIFO flush done, init complete
 *   (main loop then blinks LED4 as heartbeat)
 * ================================================================ */
#define GPIOE_ODR_DBG   (*(volatile uint32_t *)(0x40021000UL + 0x14))
#define DBG_LED2        (1u << 0)
#define DBG_LED3        (1u << 1)
#define DBG_LED4        (1u << 2)

/* Short busy-wait delay (approximate, based on HSI16) */
static void udelay(volatile uint32_t n) { while (n--) __asm__("nop"); }

/* ================================================================
 * usb_dev_init — configure clocks, GPIO, and OTG_HS peripheral
 * ================================================================ */

void usb_dev_init(void) {

    /* Stage 1: entered init -------------------------------------------- */
    GPIOE_ODR_DBG = (GPIOE_ODR_DBG & ~(DBG_LED2|DBG_LED3|DBG_LED4)) | DBG_LED2;

    /* 1. HSI48 + CRS — 48 MHz USB clock without external crystal ---------- */

    RCC_CR |= (1u << 26);                          /* HSI48ON */
    while (!(RCC_CR & (1u << 27))) {}              /* wait HSI48RDY */

    /* Select HSI48 as 48 MHz source: RCC_DCKCFGR2 CLK48SEL[27:26] = 11
     * STM32F72x offset is 0x8C (F74x with PLLSAI uses 0x90) */
    RCC_DCKCFGR2 = (RCC_DCKCFGR2 & ~(3u << 26)) | (3u << 26);

    RCC_APB1ENR |= (1u << 27);                     /* CRSEN */

    /* CRS auto-trim to USB SOF (1 kHz):
     *   RELOAD = 0xBB7F → 48 000 000 / 1000 - 1
     *   FELIM  = 22     → ~1% tolerance
     *   SYNCSRC[29:28] = 10 → USB SOF */
    CRS_CFGR = 0xBB7Fu | (22u << 16) | (2u << 28);
    CRS_CR  |= (1u << 6) | (1u << 5);             /* AUTOTRIMEN | CEN */

    /* 2. GPIO: PB14/PB15 as OTG_HS_DM/DP, AF12 -------------------------- */

    RCC_AHB1ENR |= (1u << 1);                      /* GPIOBEN */

    /* AF mode (MODER = 10) */
    GPIOB_MODER   = (GPIOB_MODER   & ~((3u<<28)|(3u<<30))) | ((2u<<28)|(2u<<30));
    /* Very high speed (OSPEEDR = 11) */
    GPIOB_OSPEEDR |= (3u<<28)|(3u<<30);
    /* AF12 = 0xC in AFRH: PB14[27:24], PB15[31:28] */
    GPIOB_AFRH    = (GPIOB_AFRH    & ~((0xFu<<24)|(0xFu<<28))) | ((0xCu<<24)|(0xCu<<28));

    /* 3. Enable OTG_HS peripheral clock (AHB1 bit 29) -------------------- */
    /* Do NOT enable ULPI clock (bit 30) — internal FS PHY only */
    RCC_AHB1ENR |= (1u << 29);

    /* Short delay for clock to propagate before accessing OTG registers */
    udelay(1000);

    /* Stage 2: clocks + GPIO done ---------------------------------------- */
    GPIOE_ODR_DBG |= DBG_LED3;

    /* 4. Select internal FS PHY (PHYSEL) — set before core reset ---------- */
    GUSBCFG |= (1u << 6);   /* PHYSEL: internal FS serial transceiver */

    /* 5. Core soft reset — with timeout so we don't hang forever ---------- */
    uint32_t timeout;
    timeout = 200000;
    while (!(GRSTCTL & (1u << 31)) && --timeout) {}  /* AHBIDL */
    GRSTCTL |= (1u << 0);                              /* CSRST */
    timeout = 200000;
    while ((GRSTCTL & (1u << 0)) && --timeout) {}     /* wait done */
    udelay(30);

    /* Re-set PHYSEL after core reset — CSRST may clear it */
    GUSBCFG |= (1u << 6);   /* PHYSEL: internal FS serial transceiver */

    /* 6. Power up PHY; assert BVAL override before mode switch so the
     * OTG session logic sees a valid B-session the moment CMOD goes to 0 */
    GCCFG = (1u << 16);                /* PWRDWN=1 (active), VBDEN=0 */
    GOTGCTL |= (1u << 7) | (1u << 6); /* BVALOVAL=1, BVALOEN=1 */
    PCGCCTL = 0;                        /* ensure PHY clocks not gated */

    /* 7. Force device mode; poll GINTSTS.CMOD until = 0 (device mode) ----- */
    GUSBCFG = (GUSBCFG & ~(1u << 29)) | (1u << 30);  /* clear FHMOD, set FDMOD */
    timeout = 3200000;  /* ~200 ms at HSI16 */
    while ((GINTSTS & (1u << 0)) && --timeout) {}  /* wait CMOD=0 */
    if (GINTSTS & (1u << 0)) {
        /* Mode switch timed out — blink LED3 rapidly to signal failure */
        while (1) {
            for (int i = 0; i < 5; i++) {
                GPIOE_ODR_DBG |=  DBG_LED3; udelay(80000);
                GPIOE_ODR_DBG &= ~DBG_LED3; udelay(80000);
            }
            udelay(400000);
        }
    }

    /* 8. Device config: full-speed via internal PHY (DSPD[1:0] = 11) ----- */
    DCFG = (DCFG & ~(3u << 0)) | (3u << 0);

    /* Stage 3: past reset, about to flush FIFOs -------------------------- */
    GPIOE_ODR_DBG |= DBG_LED4;

    /* 9. FIFO sizes (32-bit words):
     *   RX FIFO:  128 words (512 B) — shared, starts at offset 0
     *   EP0 TX:    64 words (256 B) — starts at offset 128            */
    GRXFSIZ   = 128u;
    GNPTXFSIZ = (64u << 16) | 128u;    /* NPTXFD=64, NPTXFSA=128 */

    /* 10. Flush TX (all) and RX FIFOs — with timeout -------------------- */
    GRSTCTL |= (0x10u << 6) | (1u << 5);  /* TXFNUM=all, TXFFLSH */
    timeout = 200000;
    while ((GRSTCTL & (1u << 5)) && --timeout) {}
    GRSTCTL |= (1u << 4);                  /* RXFFLSH */
    timeout = 200000;
    while ((GRSTCTL & (1u << 4)) && --timeout) {}

    /* 11. Interrupt mask ------------------------------------------------- */
    GINTSTS = 0xFFFFFFFFu;   /* clear all pending */
    GINTMSK = (1u << 12)     /* USBRST:  bus reset */
            | (1u << 13)     /* ENUMDNE: enumeration done */
            | (1u <<  4)     /* RXFLVL:  RX FIFO non-empty */
            | (1u << 18)     /* IEPINT:  IN endpoint event */
            | (1u << 19);    /* OEPINT:  OUT endpoint event */

    GAHBCFG |= (1u << 0);   /* GINT: enable interrupt output */

    /* 12. Connect: clear soft-disconnect --------------------------------- */
    DCTL &= ~(1u << 1);     /* clear SDIS */

    /* Stage 4: init complete — LED3 off, LED4 will blink from main loop -- */
    GPIOE_ODR_DBG &= ~DBG_LED3;
}

/* ================================================================
 * usb_dev_poll — service pending USB events; call from main loop
 * ================================================================ */

void usb_dev_poll(void) {
    /* LED3 = device mode active (GINTSTS.CMOD=0); off = host mode */
    if (GINTSTS & (1u << 0))
        GPIOE_ODR_DBG &= ~DBG_LED3;
    else
        GPIOE_ODR_DBG |=  DBG_LED3;

    uint32_t sts = GINTSTS & GINTMSK;
    if (!sts) return;

    /* USB bus reset ---------------------------------------------------- */
    if (sts & (1u << 12)) {
        GINTSTS = (1u << 12);
        handle_reset();
    }

    /* Enumeration done — host determined our speed ---------------------- */
    if (sts & (1u << 13)) {
        GINTSTS = (1u << 13);
        DIEPCTL0 &= ~(3u << 0);    /* MPSIZ=00: 64 B for full-speed */
        DCTL     |=  (1u << 8);    /* CGINAK: clear global IN NAK */
    }

    /* RX FIFO non-empty — OUT/SETUP packet arrived on EP0 -------------- */
    if (sts & (1u << 4)) {
        uint32_t rxsts  = GRXSTSP;
        uint8_t  pktsts = (uint8_t)((rxsts >> 17) & 0xFu);
        uint16_t bcnt   = (uint16_t)((rxsts >> 4) & 0x7FFu);

        if (pktsts == 6u && bcnt == 8u) {
            /* SETUP data received: read 2 words = 8 bytes */
            uint8_t setup[8];
            fifo_read(setup, 2);
            handle_setup(setup);
        } else if (pktsts == 4u) {
            /* SETUP stage complete: re-arm for next SETUP */
            ep0_rearm();
        } else {
            /* Drain unexpected data so FIFO doesn't block */
            uint16_t words = (bcnt + 3u) / 4u;
            while (words--) (void)DFIFO0;
        }
        /* RXFLVL clears itself when FIFO is empty */
    }

    /* IN endpoint 0 event ----------------------------------------------- */
    if (sts & (1u << 18)) {
        uint32_t iepint = DIEPINT0;
        DIEPINT0 = iepint;  /* clear by writing 1s */

        if ((iepint & (1u << 0)) && pending_addr) {
            /* XFRC: transfer complete — safe to apply SET_ADDRESS now */
            DCFG = (DCFG & ~(0x7Fu << 4)) | ((uint32_t)pending_addr << 4);
            pending_addr = 0;
        }
    }

    /* OUT endpoint 0 event ---------------------------------------------- */
    if (sts & (1u << 19)) {
        uint32_t oepint = DOEPINT0;
        DOEPINT0 = oepint;
    }
}
