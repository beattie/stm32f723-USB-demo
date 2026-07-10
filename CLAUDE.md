# STM32F723 USB Passthrough Demo

Custom board bring-up and USB password manager demo in C.

## Board

- SoC: STM32F723VET6, Cortex-M7, LQFP100, 0.5mm pitch, 512KB flash, 256KB SRAM
- Power: USB-C input → AMS1117 3.3V LDO
- Debug: SWD via LPC-LINK2 CMSIS-DAP (1fc9:0090) or STLink V2-1 (0483:374b)

## Toolchain

- Compiler: arm-none-eabi-gcc
- Flash: probe-rs
- No external crystal — use HSI16 + PLL for SYSCLK, HSI48 + CRS for USB

## Flash Commands

```bash
probe-rs download --chip STM32F723VETx --probe 1fc9:0090 bringup/bringup.elf
probe-rs reset --chip STM32F723VETx --probe 1fc9:0090
```

## Project Structure

```
bringup/    bare-metal C bring-up firmware (in progress)
kicad/      schematic and PCB (USB-pasthrough.kicad_sch)
Pictures/   board photos
```

## GPIO Map

| Signal   | Pin  | Net       |
|----------|------|-----------|
| LED1     | ?    | Power LED (always on via R9 330Ω) — not GPIO |
| LED2     | PE0  | BLUE LED (R19 33Ω bodge needed — metric 0603 footprint) |
| LED3     | PE1  | GREEN LED |
| LED4     | PE2  | YELLOW LED |
| LED5_R   | PB4  | RGB RED |
| LED5_G   | PB5  | RGB GREEN |
| LED5_B   | PB6  | RGB BLUE |
| SWDIO    | PA13 | Debug |
| SWCLK    | PA14 | Debug |
| USBA+    | PB15 (pin 71) | USB-A host D+ |
| USBA-    | PB14 (pin 70) | USB-A host D- (pin 69 disconnected — OK) |
| USBC+    | PB15 | USB-C device D+ |
| USBC-    | PB14 | USB-C device D- |
| MMC1_CLK | ?    | SD card clock |
| MMC1_CMD | ?    | SD card command |
| MMC1_D0-3| ?    | SD card data |
| SPI1_SCK | ?    | Display SCK |
| SPI1_MOSI| ?    | Display MOSI |
| SPI1_MISO| ?    | Display MISO |
| LCD_CS   | ?    | Display CS |
| LCD_DC   | ?    | Display D/C |
| LCD_BL   | ?    | Display backlight |
| LCD_RST  | pin 69 | Disconnected — not needed |

## Board v0.1 Known Issues

- LED current limiter footprints: R_0201_0603Metric where 0603 imperial needed
- VDDA bypass caps were in series (schematic bug) — workaround applied
- VDDPHYHS caps same series bug — apply same fix when installing FB2/C23/C24
- Pin 69 (LCD_RST) disconnected — not a problem, LCD_RST not needed for demo
- No UART on board — use SWO/ITM via J7 for debug output

## Bring-Up Status

- [x] Smoke test — SWD connects, VCAP reads 1.2V, VDD 3.285V, VDDA 3.2V
- [x] LED chase firmware written and verified (bringup/main.c) — blue LED confirmed blinking 2026-07-10
- [x] Install FB2 + C23 + C24 (VDDPHYHS filter, parallel cap fix) — done 2026-07-10
- [ ] Bodge R19, install LED2 — flash LED2 blink on PE0
- [ ] USB-A host bring-up
- [ ] USB-C device bring-up
- [ ] MicroSD bring-up
- [ ] Port password manager demo from stm32f746-disc (Rust) to C

## Reference Project

Password manager logic implemented in Rust/Embassy at ~/projects/stm32f746-disc.
See src/usb_fs_host.rs, src/pm.rs, src/usb_hs.rs for the state machine to port.
