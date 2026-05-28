#include <stdint.h>

extern uint32_t _data_start, _data_end, _data_load;
extern uint32_t _bss_start, _bss_end;
extern uint32_t _stack_top;

void main(void);

void Default_Handler(void) { while (1) {} }

void Reset_Handler(void) {
    /* copy .data from flash to SRAM */
    uint32_t *src = &_data_load;
    uint32_t *dst = &_data_start;
    while (dst < &_data_end) *dst++ = *src++;

    /* zero .bss */
    dst = &_bss_start;
    while (dst < &_bss_end) *dst++ = 0;

    main();
    while (1) {}
}

/* minimal vector table */
__attribute__((section(".vectors")))
uint32_t vectors[] = {
    (uint32_t)&_stack_top,
    (uint32_t)Reset_Handler,
    (uint32_t)Default_Handler,  /* NMI */
    (uint32_t)Default_Handler,  /* HardFault */
    (uint32_t)Default_Handler,  /* MemManage */
    (uint32_t)Default_Handler,  /* BusFault */
    (uint32_t)Default_Handler,  /* UsageFault */
};
