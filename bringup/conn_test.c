/*
 * conn_test.c — GPIO connectivity test for STM32F723 custom board v0.1
 *
 * Standalone firmware (has its own main, replaces bringup for testing).
 * Build with: make flash_test
 *
 * All USB data lines, MMC data/clock/cmd, and SPI1_SCK are driven as plain
 * GPIO outputs with distinct toggle rates.  Probe each pin on the PCB or
 * connector with a scope to verify traces and solder joints.
 *
 * Outputs (all GPIO, no alternate function):
 *   PA5  SPI1_SCK  pin 29  ~1 Hz  — 1PPS reference; jump to PA10 for input test
 *   PA11 USBA_DM   pin 70  ~8 Hz
 *   PA12 USBA_DP   pin 71  ~16 Hz
 *   PB14 USBC_DM   pin 56  ~32 Hz
 *   PB15 USBC_DP   pin 57  ~64 Hz
 *   PC8  MMC_D0    pin 65  ~128 Hz
 *   PC9  MMC_D1    pin 66  ~256 Hz
 *   PC10 MMC_D2    pin 77  ~512 Hz
 *   PC11 MMC_D3    pin 78  ~1 kHz
 *   PC12 MMC_CLK   pin 79  ~2 kHz
 *   PD2  MMC_CMD   pin 82  ~4 kHz
 *
 * Input:
 *   PA10 LCD_RST / OTG_FS_ID  pin 69 — input with pull-down
 *   Jump PA5 → PA10 to verify input path.
 *   LED3 (PE1 GREEN)  lights while PA10 is high.
 *   LED4 (PE2 YELLOW) mirrors PA5 (~1 Hz heartbeat).
 *
 * Frequencies are approximate; they scale with CPU speed.
 * At HSI16 (16 MHz) with -O0 the counter runs at ~100–150 k iter/s,
 * so bit 17 ≈ 1 Hz, each lower bit doubles the frequency.
 */

#include <stdint.h>

/* ---- RCC ---- */
#define RCC_AHB1ENR  (*(volatile uint32_t *)0x40023830UL)

/* ---- GPIO helpers ---- */
/* Port indices: A=0, B=1, C=2, D=3, E=4 */
#define GPIO_BASE(p)   (0x40020000UL + (uint32_t)(p) * 0x400UL)
#define MODER(p)       (*(volatile uint32_t *)(GPIO_BASE(p) + 0x00))
#define OSPEEDR(p)     (*(volatile uint32_t *)(GPIO_BASE(p) + 0x08))
#define PUPDR(p)       (*(volatile uint32_t *)(GPIO_BASE(p) + 0x0C))
#define IDR(p)         (*(volatile uint32_t *)(GPIO_BASE(p) + 0x10))
#define BSRR(p)        (*(volatile uint32_t *)(GPIO_BASE(p) + 0x18))

#define PA 0
#define PB 1
#define PC 2
#define PD 3
#define PE 4

/* Atomic set (val=1) or reset (val=0) of a single pin via BSRR */
static inline void pin_out(int port, int pin, uint32_t val)
{
    if (val) BSRR(port) = (1u << pin);
    else     BSRR(port) = (1u << (pin + 16));
}

/* Configure a pin as push-pull output, very-high-speed */
static void cfg_out(int port, int pin)
{
    MODER(port)   = (MODER(port)   & ~(3u << (pin * 2))) | (1u << (pin * 2));
    OSPEEDR(port) = (OSPEEDR(port) | (3u << (pin * 2)));
}

/* Configure a pin as floating input with pull-down */
static void cfg_in_pd(int port, int pin)
{
    MODER(port) &= ~(3u << (pin * 2));                                 /* input */
    PUPDR(port)  = (PUPDR(port) & ~(3u << (pin * 2))) | (2u << (pin * 2)); /* pull-down */
}

int main(void)
{
    /* Enable GPIOA–E clocks */
    RCC_AHB1ENR |= (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4);

    /* Output pins */
    cfg_out(PA,  5);   /* SPI1_SCK  — 1PPS reference */
    cfg_out(PA, 11);   /* USBA_DM  */
    cfg_out(PA, 12);   /* USBA_DP  */
    cfg_out(PB, 14);   /* USBC_DM  */
    cfg_out(PB, 15);   /* USBC_DP  */
    cfg_out(PC,  8);   /* MMC_D0   */
    cfg_out(PC,  9);   /* MMC_D1   */
    cfg_out(PC, 10);   /* MMC_D2   */
    cfg_out(PC, 11);   /* MMC_D3   */
    cfg_out(PC, 12);   /* MMC_CLK  */
    cfg_out(PD,  2);   /* MMC_CMD  */
    cfg_out(PE,  1);   /* LED3 (GREEN)  */
    cfg_out(PE,  2);   /* LED4 (YELLOW) */

    /* Input pin: PA10 (LCD_RST / OTG_FS_ID) — jumper to PA5 to test */
    cfg_in_pd(PA, 10);

    uint32_t c = 0;
    while (1) {
        c++;

        /*
         * Each output is driven by one bit of the counter.
         * Bit N gives a square wave with period = 2^(N+1) iterations.
         * At ~120 k iter/s (HSI16, -O0):
         *   bit 17 ≈ 1 Hz, bit 14 ≈ 8 Hz, …, bit 5 ≈ 4 kHz
         */
        pin_out(PA,  5, (c >> 17) & 1);   /* ~1 Hz  — 1PPS */
        pin_out(PA, 11, (c >> 14) & 1);   /* ~8 Hz  */
        pin_out(PA, 12, (c >> 13) & 1);   /* ~16 Hz */
        pin_out(PB, 14, (c >> 12) & 1);   /* ~32 Hz */
        pin_out(PB, 15, (c >> 11) & 1);   /* ~64 Hz */
        pin_out(PC,  8, (c >> 10) & 1);   /* ~128 Hz */
        pin_out(PC,  9, (c >>  9) & 1);   /* ~256 Hz */
        pin_out(PC, 10, (c >>  8) & 1);   /* ~512 Hz */
        pin_out(PC, 11, (c >>  7) & 1);   /* ~1 kHz */
        pin_out(PC, 12, (c >>  6) & 1);   /* ~2 kHz */
        pin_out(PD,  2, (c >>  5) & 1);   /* ~4 kHz */

        /* LED3: lit while PA10 is high (jumpered to PA5) */
        pin_out(PE, 1, (IDR(PA) >> 10) & 1);

        /* LED4: mirrors PA5 — visible heartbeat */
        pin_out(PE, 2, (c >> 17) & 1);
    }
    return 0;
}
