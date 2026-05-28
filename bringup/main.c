#include <stdint.h>

/* RCC */
#define RCC_BASE        0x40023800
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_AHB1ENR_GPIOBEN  (1 << 1)
#define RCC_AHB1ENR_GPIOEEN  (1 << 4)

/* GPIOB: PB4=LED5_R, PB5=LED5_G, PB6=LED5_B */
#define GPIOB_BASE      0x40020400
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_ODR       (*(volatile uint32_t *)(GPIOB_BASE + 0x14))

/* GPIOE: PE0=LED2, PE1=LED3, PE2=LED4 */
#define GPIOE_BASE      0x40021000
#define GPIOE_MODER     (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_ODR       (*(volatile uint32_t *)(GPIOE_BASE + 0x14))

/* LED bit masks */
#define LED5_R  (1 << 4)   /* PB4 */
#define LED5_G  (1 << 5)   /* PB5 */
#define LED5_B  (1 << 6)   /* PB6 */
#define LED2    (1 << 0)   /* PE0 */
#define LED3    (1 << 1)   /* PE1 */
#define LED4    (1 << 2)   /* PE2 */

static void delay(volatile uint32_t n) {
    while (n--) __asm__("nop");
}

void main(void) {
    /* enable GPIOB and GPIOE clocks */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOEEN;

    /* set PB4, PB5, PB6 to output (MODER bits: 01 = output) */
    GPIOB_MODER &= ~((3 << 8) | (3 << 10) | (3 << 12));
    GPIOB_MODER |=  ((1 << 8) | (1 << 10) | (1 << 12));

    /* set PE0, PE1, PE2 to output */
    GPIOE_MODER &= ~((3 << 0) | (3 << 2) | (3 << 4));
    GPIOE_MODER |=  ((1 << 0) | (1 << 2) | (1 << 4));

    /* all LEDs off */
    GPIOB_ODR &= ~(LED5_R | LED5_G | LED5_B);
    GPIOE_ODR &= ~(LED2 | LED3 | LED4);

    /* sequence: chase through all 6 LEDs */
    uint32_t step = 0;
    while (1) {
        GPIOB_ODR &= ~(LED5_R | LED5_G | LED5_B);
        GPIOE_ODR &= ~(LED2 | LED3 | LED4);

        switch (step % 6) {
            case 0: GPIOE_ODR |= LED2;   break;
            case 1: GPIOE_ODR |= LED3;   break;
            case 2: GPIOE_ODR |= LED4;   break;
            case 3: GPIOB_ODR |= LED5_R; break;
            case 4: GPIOB_ODR |= LED5_G; break;
            case 5: GPIOB_ODR |= LED5_B; break;
        }
        step++;
        delay(500000);  /* ~HSI 16MHz, adjust as needed */
    }
}
