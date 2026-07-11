#include <stdint.h>
#include "usb_dev.h"

/* RCC */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))

/* GPIOE: PE0=LED2(BLUE), PE1=LED3(GREEN), PE2=LED4(YELLOW) */
#define GPIOE_BASE      0x40021000UL
#define GPIOE_MODER     (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_ODR       (*(volatile uint32_t *)(GPIOE_BASE + 0x14))

/* LED bit masks */
#define LED2    (1u << 0)   /* PE0 — BLUE */
#define LED3    (1u << 1)   /* PE1 — GREEN */
#define LED4    (1u << 2)   /* PE2 — YELLOW */

static void delay(volatile uint32_t n) {
    while (n--) __asm__("nop");
}

int main(void) {
    /* Enable GPIOE clock */
    RCC_AHB1ENR |= (1u << 4);  /* GPIOEEN */

    /* PE0, PE1, PE2 as outputs */
    GPIOE_MODER &= ~((3u<<0)|(3u<<2)|(3u<<4));
    GPIOE_MODER |=  ((1u<<0)|(1u<<2)|(1u<<4));
    GPIOE_ODR   &= ~(LED2|LED3|LED4);

    /* Startup: blink each LED 3x for individual verification */
    uint32_t leds[] = { LED2, LED3, LED4 };
    for (int i = 0; i < 3; i++) {
        for (int n = 0; n < 3; n++) {
            GPIOE_ODR |=  leds[i]; delay(200000);
            GPIOE_ODR &= ~leds[i]; delay(200000);
        }
    }
    delay(400000);

    /* Initialise USB-C device (OTG_HS FS, PB14/PB15) */
    usb_dev_init();

    /* Main loop: poll USB events; blink LED4 as ~1 Hz heartbeat */
    uint32_t counter = 0;
    while (1) {
        usb_dev_poll();
        if (++counter >= 200000) {
            counter = 0;
            GPIOE_ODR ^= LED4;
        }
    }
    return 0;
}
