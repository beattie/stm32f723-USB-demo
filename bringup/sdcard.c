/*
 * sdcard.c — MicroSD bring-up via SDMMC1 on STM32F723
 *
 * PC8=D0, PC9=D1, PC10=D2, PC11=D3, PC12=CLK  (AF12, GPIOC)
 * PD2=CMD                                       (AF12, GPIOD)
 * PE3=CDET                                      (active-low input, GPIOE)
 *
 * Clock: SDMMC1 uses CLK48 (HSI48, set up by usb_host_init).
 * Init sequence: CMD0 → CMD8 → ACMD41 → CMD2 → CMD3 → CMD7.
 * Leaves card in TRAN state at 400 kHz, 1-bit bus (bring-up only).
 *
 * LED3 (PE1) is on when a card is present and initialised.
 */

#include <stdint.h>
#include "sdcard.h"

/* ================================================================
 * RCC
 * ================================================================ */
#define RCC_BASE        0x40023800UL
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x44))
#define RCC_DCKCFGR2    (*(volatile uint32_t *)(RCC_BASE + 0x90))

/* ================================================================
 * GPIO
 * ================================================================ */
#define GPIOC_BASE      0x40020800UL
#define GPIOC_MODER     (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_OSPEEDR   (*(volatile uint32_t *)(GPIOC_BASE + 0x08))
#define GPIOC_PUPDR     (*(volatile uint32_t *)(GPIOC_BASE + 0x0C))
#define GPIOC_AFRH      (*(volatile uint32_t *)(GPIOC_BASE + 0x24))

#define GPIOD_BASE      0x40020C00UL
#define GPIOD_MODER     (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OSPEEDR   (*(volatile uint32_t *)(GPIOD_BASE + 0x08))
#define GPIOD_PUPDR     (*(volatile uint32_t *)(GPIOD_BASE + 0x0C))
#define GPIOD_AFRL      (*(volatile uint32_t *)(GPIOD_BASE + 0x20))

#define GPIOE_BASE      0x40021000UL
#define GPIOE_MODER     (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_PUPDR     (*(volatile uint32_t *)(GPIOE_BASE + 0x0C))
#define GPIOE_IDR       (*(volatile uint32_t *)(GPIOE_BASE + 0x10))
#define GPIOE_ODR       (*(volatile uint32_t *)(GPIOE_BASE + 0x14))

#define LED3            (1u << 1)   /* PE1 — GREEN */
#define CDET_PIN        (1u << 3)   /* PE3 — card detect, active-low */

/* ================================================================
 * SDMMC1 (APB2, base 0x40012C00)
 * ================================================================ */
#define SDMMC_BASE      0x40012C00UL
#define SDMMC_POWER     (*(volatile uint32_t *)(SDMMC_BASE + 0x00))
#define SDMMC_CLKCR     (*(volatile uint32_t *)(SDMMC_BASE + 0x04))
#define SDMMC_ARG       (*(volatile uint32_t *)(SDMMC_BASE + 0x08))
#define SDMMC_CMD       (*(volatile uint32_t *)(SDMMC_BASE + 0x0C))
#define SDMMC_RESP1     (*(volatile uint32_t *)(SDMMC_BASE + 0x14))
#define SDMMC_RESP2     (*(volatile uint32_t *)(SDMMC_BASE + 0x18))
#define SDMMC_RESP3     (*(volatile uint32_t *)(SDMMC_BASE + 0x1C))
#define SDMMC_RESP4     (*(volatile uint32_t *)(SDMMC_BASE + 0x20))
#define SDMMC_STA       (*(volatile uint32_t *)(SDMMC_BASE + 0x34))
#define SDMMC_ICR       (*(volatile uint32_t *)(SDMMC_BASE + 0x38))

/* STA / ICR bits */
#define STA_CCRCFAIL    (1u << 0)
#define STA_CTIMEOUT    (1u << 2)
#define STA_CMDREND     (1u << 6)
#define STA_CMDSENT     (1u << 7)

/* CLKCR: SDMMCCLK=48 MHz → 400 kHz init clock
 * CK = SDMMCCLK / (CLKDIV + 2) → CLKDIV = 48000000/400000 - 2 = 118 */
#define CLKCR_400KHZ    (118u | (1u << 8))   /* CLKDIV=118, CLKEN=1 */

/* ================================================================
 * Module state
 * ================================================================ */
static volatile int g_ready = 0;
static uint16_t     g_rca   = 0;   /* relative card address */

/* ================================================================
 * Helpers
 * ================================================================ */
static void udelay(volatile uint32_t n) { while (n--) __asm__("nop"); }

static void fail_blink(int count)
{
    /* Blink LED3 'count' times repeatedly to indicate which init step failed */
    while (1) {
        for (int i = 0; i < count; i++) {
            GPIOE_ODR |=  LED3; udelay(200000);
            GPIOE_ODR &= ~LED3; udelay(200000);
        }
        udelay(800000);
    }
}

/*
 * send_cmd — send an SD command and wait for completion.
 *
 * waitresp: 0=no response, 1=short (R1/R6/R7), 3=long (R2)
 *
 * Returns:
 *   0  = CMDREND (short/long response received, CRC OK)
 *   1  = CCRCFAIL (response received, CRC failed — expected for R3)
 *  -1  = CTIMEOUT
 *  -2  = loop timeout (peripheral stuck)
 */
static int send_cmd(uint8_t cmd, uint32_t arg, uint8_t waitresp, uint32_t *resp)
{
    SDMMC_ICR = 0xC007FFFFu;    /* clear all clearable flags */
    SDMMC_ARG = arg;
    SDMMC_CMD = ((uint32_t)cmd & 0x3Fu)
              | ((uint32_t)waitresp << 6)
              | (1u << 10);     /* CPSMEN */

    uint32_t done = (waitresp == 0) ? STA_CMDSENT
                                    : (STA_CMDREND | STA_CCRCFAIL | STA_CTIMEOUT);
    uint32_t timeout = 2000000;
    uint32_t sta;
    do { sta = SDMMC_STA; } while (!(sta & done) && --timeout);

    if (!timeout)              return -2;
    if (sta & STA_CTIMEOUT)    return -1;

    if (resp && waitresp != 0) {
        resp[0] = SDMMC_RESP1;
        if (waitresp == 3) {
            resp[1] = SDMMC_RESP2;
            resp[2] = SDMMC_RESP3;
            resp[3] = SDMMC_RESP4;
        }
    }

    return (sta & STA_CCRCFAIL) ? 1 : 0;
}

/* ================================================================
 * card_init — run the SD init sequence (blocking)
 * Returns 0 on success, negative on failure.
 * ================================================================ */
static int card_init(void)
{
    uint32_t resp[4];
    int rc;

    /* CMD0: GO_IDLE_STATE — reset card */
    send_cmd(0, 0, 0, 0);
    udelay(2000);

    /* CMD8: SEND_IF_COND — check for SDHC (VHS=1, check=0xAA) */
    int sdhc = 0;
    rc = send_cmd(8, 0x000001AAu, 1, resp);
    if (rc == 0 && (resp[0] & 0xFFFu) == 0x1AAu)
        sdhc = 1;
    /* Timeout here means SD v1.x (no SDHC) — that's OK, continue */

    /* ACMD41: SD_APP_OP_COND — wait for card to leave busy state.
     * ACMD = CMD55 (set RCA=0) then CMD41.
     * HCS bit 30 set if SDHC supported. */
    uint32_t acmd41_arg = (1u << 20)            /* voltage window 3.2–3.4V */
                        | (sdhc ? (1u << 30) : 0u); /* HCS */

    int tries = 1000;
    while (tries--) {
        /* CMD55: APP_CMD (RCA=0 during init) */
        rc = send_cmd(55, 0, 1, resp);
        if (rc < 0) return -2;

        /* CMD41: SD_APP_OP_COND — R3 response, CRC fail expected */
        rc = send_cmd(41, acmd41_arg, 1, resp);
        if (rc == -1) return -3;   /* timeout = no card */
        /* rc==1 (CRC fail) is normal for R3 */

        if (resp[0] & (1u << 31))  /* power-up status bit: card ready */
            break;

        udelay(10000);
    }
    if (!tries) return -4;  /* card never became ready */

    /* CMD2: ALL_SEND_CID — get CID (R2, 136-bit long response) */
    rc = send_cmd(2, 0, 3, resp);
    if (rc < 0) return -5;

    /* CMD3: SEND_RELATIVE_ADDR — card publishes its RCA */
    rc = send_cmd(3, 0, 1, resp);
    if (rc != 0) return -6;
    g_rca = (uint16_t)(resp[0] >> 16);

    /* CMD7: SELECT_CARD — move to TRAN state */
    rc = send_cmd(7, (uint32_t)g_rca << 16, 1, resp);
    if (rc != 0) return -7;

    return 0;
}

/* ================================================================
 * sdcard_init
 * ================================================================ */
void sdcard_init(void)
{
    /* 1. HSI48: ensure it's on (usb_host_init may have done this already) */
    RCC_CR |= (1u << 26);               /* HSI48ON */
    while (!(RCC_CR & (1u << 27))) {}   /* HSI48RDY */
    /* CLK48SEL = 11: HSI48 → CLK48 (also source for SDMMC1) */
    RCC_DCKCFGR2 = (RCC_DCKCFGR2 & ~(3u << 26)) | (3u << 26);
    /* SDMMCSEL bit 28 = 0: use CLK48 source (default) */

    /* 2. Enable GPIO clocks */
    RCC_AHB1ENR |= (1u << 2)   /* GPIOCEN */
                 | (1u << 3)   /* GPIODEN */
                 | (1u << 4);  /* GPIOEEN (also used for LEDs) */

    /* 3. GPIOC: PC8–PC12 as AF12 (SDMMC1 D0–D3, CLK) */
    /* MODER: set bits [25:16] to 10101010101010101010 = AF for pins 8–12 */
    GPIOC_MODER = (GPIOC_MODER & ~(  (3u<<16)|(3u<<18)|(3u<<20)|(3u<<22)|(3u<<24) ))
                               |    ( (2u<<16)|(2u<<18)|(2u<<20)|(2u<<22)|(2u<<24) );
    /* Very high speed */
    GPIOC_OSPEEDR |= (3u<<16)|(3u<<18)|(3u<<20)|(3u<<22)|(3u<<24);
    /* Pull-up on D0–D3 (PC8–PC11); no pull on CLK (PC12) */
    GPIOC_PUPDR = (GPIOC_PUPDR & ~( (3u<<16)|(3u<<18)|(3u<<20)|(3u<<22)|(3u<<24) ))
                               |  ( (1u<<16)|(1u<<18)|(1u<<20)|(1u<<22) );   /* PC12: no pull */
    /* AFRH: AF12=0xC for PC8[3:0], PC9[7:4], PC10[11:8], PC11[15:12], PC12[19:16] */
    GPIOC_AFRH = (GPIOC_AFRH & ~( (0xFu<<0)|(0xFu<<4)|(0xFu<<8)|(0xFu<<12)|(0xFu<<16) ))
                             |  ( (0xCu<<0)|(0xCu<<4)|(0xCu<<8)|(0xCu<<12)|(0xCu<<16) );

    /* 4. GPIOD: PD2 as AF12 (SDMMC1 CMD) */
    GPIOD_MODER   = (GPIOD_MODER   & ~(3u<<4))  | (2u<<4);
    GPIOD_OSPEEDR |= (3u<<4);
    GPIOD_PUPDR   = (GPIOD_PUPDR   & ~(3u<<4))  | (1u<<4);  /* pull-up */
    GPIOD_AFRL    = (GPIOD_AFRL    & ~(0xFu<<8)) | (0xCu<<8); /* PD2 = AF12 */

    /* 5. GPIOE: PE3 as input with pull-up (CDET, active-low) */
    GPIOE_MODER = (GPIOE_MODER & ~(3u<<6));        /* input */
    GPIOE_PUPDR = (GPIOE_PUPDR & ~(3u<<6)) | (1u<<6); /* pull-up */

    /* 6. Enable SDMMC1 clock (APB2 bit 11) */
    RCC_APB2ENR |= (1u << 11);
    udelay(1000);

    /* 7. Power on SDMMC1 */
    SDMMC_POWER = 3u;       /* PWRCTRL = 11: power on */
    udelay(4000);           /* ≥ 74 SD clock cycles at 400 kHz ≈ 185 µs */

    /* 8. Start 400 kHz clock */
    SDMMC_CLKCR = CLKCR_400KHZ;

    /* 9. Check card detect before attempting init */
    if (GPIOE_IDR & CDET_PIN) {
        /* No card inserted — return silently; poll() will retry */
        return;
    }

    /* 10. Run card init sequence */
    if (card_init() != 0) {
        fail_blink(3);  /* 3 blinks = card init failed */
    }

    g_ready = 1;
    GPIOE_ODR |= LED3;
}

/* ================================================================
 * sdcard_poll — call from main loop
 * ================================================================ */
void sdcard_poll(void)
{
    int card_present = !(GPIOE_IDR & CDET_PIN);

    if (!card_present && g_ready) {
        /* Card was removed */
        g_ready = 0;
        g_rca   = 0;
        GPIOE_ODR &= ~LED3;
    } else if (card_present && !g_ready) {
        /* Card inserted — try to initialise */
        udelay(50000);  /* debounce */
        if (card_init() == 0) {
            g_ready = 1;
            GPIOE_ODR |= LED3;
        }
    }
}

/* ================================================================
 * sdcard_ready
 * ================================================================ */
int sdcard_ready(void)
{
    return g_ready;
}
