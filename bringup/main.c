#include <stdint.h>

/* RCC */
#define RCC_BASE        0x40023800
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_AHB1ENR_GPIOEEN  (1 << 4)

/* GPIOE: PE0=LED2, PE1=LED3, PE2=LED4 */
#define GPIOE_BASE      0x40021000
#define GPIOE_MODER     (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_ODR       (*(volatile uint32_t *)(GPIOE_BASE + 0x14))

/* LED bit masks */
#define LED2    (1 << 0)   /* PE0 */
#define LED3    (1 << 1)   /* PE1 */
#define LED4    (1 << 2)   /* PE2 */

static void delay(volatile uint32_t n) {
    while (n--) __asm__("nop");
}

int main(void) {
    /* enable GPIOE clock */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOEEN;

    /* set PE0, PE1, PE2 to output */
    GPIOE_MODER &= ~((3 << 0) | (3 << 2) | (3 << 4));
    GPIOE_MODER |=  ((1 << 0) | (1 << 2) | (1 << 4));

    /* all LEDs off */
    GPIOE_ODR &= ~(LED2 | LED3 | LED4);

    /* startup: blink LED2, LED3, LED4 individually for bring-up verification */
    uint32_t pe_leds[] = { LED2, LED3, LED4 };
    for (int i = 0; i < 3; i++) {
        for (int n = 0; n < 3; n++) {
            GPIOE_ODR |=  pe_leds[i];
            delay(200000);
            GPIOE_ODR &= ~pe_leds[i];
            delay(200000);
        }
    }
    delay(400000);

    /* sequence: chase through LED2, LED3, LED4 */
    uint32_t step = 0;
    while (1) {
        GPIOE_ODR &= ~(LED2 | LED3 | LED4);

        switch (step % 3) {
            case 0: GPIOE_ODR |= LED2; break;
            case 1: GPIOE_ODR |= LED3; break;
            case 2: GPIOE_ODR |= LED4; break;
        }
        step++;
        delay(500000);  /* ~HSI 16MHz, adjust as needed */
    }
	return 0;
}
