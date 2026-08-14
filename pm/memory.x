MEMORY
{
    /* STM32F723VET6: 512KB flash, 256KB SRAM
     * DTCM (64KB @ 0x20000000) + SRAM1 (192KB @ 0x20010000) are contiguous in
     * address space. DMA buffers must be in SRAM1 (0x20010000+), not DTCM. */
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
