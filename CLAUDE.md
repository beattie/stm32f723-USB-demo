# STM32F723 USB Passthrough Demo

Custom board bring-up and USB password manager demo in C.

## Board

- SoC: STM32F723VET6, Cortex-M7, LQFP100, 0.5mm pitch, 512KB flash, 256KB SRAM
- Power: USB-C input → AMS1117 3.3V LDO
- Debug: SWD via LPC-LINK2 CMSIS-DAP (1fc9:0090) or STLink V2-1 (0483:374b)

## Toolchain

- Compiler: arm-none-eabi-gcc
- Flash: probe-rs
- 8MHz crystal installed on v0.1 (bodged 0603 caps); HSE confirmed 710ppm
- Clock: PLLM=4, PLLN=192, PLLP=2 → SYSCLK=192MHz; PLLQ=8 → 48MHz for USB
- CLK48SEL on F72x only routes PLLQ — no PLLSAI option for USB clock

## Flash Commands

```bash
probe-rs download --chip STM32F723VETx --probe 1fc9:0090 ~/STM32F723_bringup/build/STM32F723_bringup.elf
probe-rs reset --chip STM32F723VETx --probe 1fc9:0090
probe-rs attach --chip STM32F723VETx --probe 1fc9:0090 ~/STM32F723_bringup/build/STM32F723_bringup.elf  # RTT
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
| LED1     | N/A  | Power LED (+5V via R9 330Ω) — not GPIO |
| LED2     | PE9  | BLUE LED — TIM1_CH1 (AF1), PWM capable |
| LED3     | PE11 | GREEN LED — TIM1_CH2 (AF1), PWM capable |
| LED4     | PE13 | YELLOW LED — TIM1_CH3 (AF1), PWM capable |
| LED5_R   | PB4  | RGB RED |
| LED5_G   | PB5  | RGB GREEN |
| LED5_B   | PB6  | RGB BLUE |
| SWDIO    | PA13 | Debug |
| SWCLK    | PA14 | Debug |
| USBA+    | PA12 (pin 71) | USB-A host D+ — OTG_FS |
| USBA-    | PA11 (pin 70) | USB-A host D- — OTG_FS |
| USBA_EN  | PB0           | USB-A VBUS enable — TPS2065C active-high EN (v0.2) |
| USBC+    | PB15 (pin 57) | USB-C device D+ — OTG_HS internal FS PHY |
| USBC-    | PB14 (pin 56) | USB-C device D- — OTG_HS internal FS PHY |
| SD1_CLK  | PC12 | SD card clock |
| SD1_CMD  | PD2  | SD card command |
| SD1_D0   | PC8  | SD card data |
| SD1_D1   | PC9  | SD card data |
| SD1_D2   | PC10 | SD card data |
| SD1_D3   | PC11 | SD card data |
| SD1_CDET | PE3  | SD card detect |
| SPI1_SCK | PA5  | Display SCK |
| SPI1_MOSI| PA7  | Display MOSI |
| SPI1_MISO| PA6  | Display MISO |
| LCD_CS   | PA4  | Display CS |
| LCD_DC   | PB1  | Display D/C (v0.2; was PA8) |
| LCD_BL   | PA15 | Display backlight |
| LCD_RST  | PA1    | Display reset (v0.2) |
| VERSION_ID | PD3–PD7 | Board version strapping inputs (v0.2) |
| UART2_TX | PA2    | USART2 TX (AF7) — v0.2 |
| UART2_RX | PA3    | USART2 RX (AF7) — v0.2 |

## Board v0.1 Known Issues

- LED current limiter footprints: R_0201_0603Metric where 0603 imperial needed
- VDDA bypass caps were in series (schematic bug) — workaround applied
- VDDPHYHS caps same series bug — apply same fix when installing FB2/C23/C24
- Pin 69 (LCD_RST / PA10) disconnected — not a problem for LCD, but PA10 is OTG_FS_ID; floating ID pin keeps OTG core in B-device/device mode. **v0.1 bodge: wire PA10 to GND. v0.2 fix: add 10kΩ pull-down on PA10.**
- PB13 (pin 52, OTG_HS_VBUS) is NC — VBUS comparator has no input, D+ pull-up never activates, USB-C device invisible to host. **v0.2 fix: connect PB13 to VBUS_DATA net (post-fuse VBUS from J8).**
- No UART on board — use SWO/ITM via J7 for debug output

## Schematic v0.2 Change List — COMPLETE

1. [x] **PA10 pull-down**: add 10kΩ from PA10 (pin 69) to GND — fixes OTG_FS_ID float, enables USB-A host mode without FHMOD hack
2. [x] **PB13 to VBUS_DATA**: connect PB13 (pin 52) to VBUS_DATA net (J8 VBUS post-fuse) — enables OTG_HS VBUS comparator for USB-C device detection
3. [x] **LED resistor footprints**: change from R_0201_0603Metric to R_0603 (imperial) for all LED current limiters
4. [x] **VDDA/VDDPHYHS cap topology**: fix bypass caps from series to parallel (schematic matches the hardware bodge already applied)
5. [x] **UART2**: add PA2 (TX) / PA3 (RX) 3-pin header for USART2 debug and ROM bootloader DFU
6. [x] **LED2/3/4 pins**: move from PE0/PE1/PE2 to PE9/PE11/PE13 (TIM1 CH1/2/3 AF1) for PWM brightness control
7. [x] **LCD_RST**: PA1 (was disconnected pin 69 / PA10)
8. [x] **Version ID inputs**: move from PE11–PE15 to PD3–PD7 (frees PE9/PE11/PE13 for LEDs)
9. [x] **SD connector**: switch from microSD to full-size SD (SD-1-A footprint); both microSD and full-size SD footprints included on PCB as an experiment — only one to be populated
10. [x] **RGB LED resistor footprints**: 0201 → 0603
11. [x] **TPS2065C USB-A VBUS switch**: EN on PB0 (active-high), replaces polyfuse — soft-start, overcurrent latch-off, GPIO-controlled power
12. [x] **LCD_DC**: moved from PA8 to PB1; PA8 broken out as test point (MCO1/OTG_FS_SOF)
13. [x] **PA9 VBUS divider removed**: fixed host mode with VBUS sensing disabled in firmware

## Bring-Up Status

- [x] Smoke test — SWD connects, VCAP reads 1.2V, VDD 3.285V, VDDA 3.2V
- [x] LED chase firmware written and verified (bringup/main.c) — blue LED confirmed blinking 2026-07-10
- [x] Install FB2 + C23 + C24 (VDDPHYHS filter, parallel cap fix) — done 2026-07-10
- [x] Bodge R19/R22/R23, install LED2/3/4 — all four LEDs verified 2026-07-10
- [x] USB-A host bring-up — AnnePro2 enumerates as LS HID keyboard, URB_DONE confirmed on keypress 2026-07-20
  - FHMOD workaround for damaged PA10 (OTG_FS_ID) confirmed working
  - SET_PROTOCOL STALL handled non-fatally (AnnePro2 quirk)
  - Keypress ASCII decoding in place; RTT output confirmed
  - Commit: aac26ba
- [ ] USB-C device bring-up — OTG_HS FS PHY init confirmed; PB13 NC on v0.1 blocks D+ pull-up; v0.2 fixes with PB13→VBUS_DATA + VBDEN=1 firmware change
- [ ] MicroSD bring-up — abandoned on v0.1 (STBITERR, broken connector GND); retry on v0.2 with full-size SD connector
- [ ] Port password manager demo from stm32f746-disc (Rust) to C

## v0.2 Status

- [x] Schematic complete — all 13 changes applied, ERC/DRC clean — 2026-07-21
- [x] PCB routed — 2026-07-21
- [ ] Fab / assemble
- [ ] USB-C device bring-up (PB13 now connected to VBUS_DATA)
- [ ] MicroSD bring-up (full-size SD connector)
- [ ] Firmware: enable VBDEN=1 for USB-C device (remove GOTGCTL bypass, use real VBUS comparator)

## DFU Notes

The STM32F723 built-in ROM bootloader uses OTG_FS (PA11/PA12) for DFU. Since
PA11/PA12 are wired to the USB-A host connector, the ROM bootloader is not usable
without a PC connected there.

DFU over USB-C (OTG_HS) requires a firmware implementation — the ROM bootloader
does not use OTG_HS. A custom DFU class could be triggered by the BOOT button.
For now, SWD via LPC-LINK2 is the flashing method.

## PCB Layout Notes (v0.2)

- Avoid placing microSD connector keep-out area beneath power vias
- v0.2: remove PA9 VBUS sensing voltage divider — fixed host mode (FHMOD=1) with VBUS sensing disabled in firmware
- Activate_SOF (PA8, OTG_FS): outputs 1ms USB Start-Of-Frame pulse on PA8 — not needed for normal operation but useful for bring-up/testing to confirm host is active and frame timing is correct; probe on scope to verify USB host enumeration is running
- SD-1-A footprint is correct; previous confusion was a schematic wiring error (D1 connected to both D0 and D1) — now fixed

## Free Pins Summary (v0.2)

Ports F/G not bonded out in LQFP100 — only ports A–E available.

| Port | Free Pins | Notable AFs |
|------|-----------|-------------|
| PA | PA0 | TIM2_CH1, TIM5_CH1 |
| PB | PB2, PB7–PB12 | PB7–9: TIM4_CH2/3/4 + I2C1; PB10/11: TIM2_CH3/4 + I2C2 |
| PC | PC0–PC7, PC13–PC15 | PC0–5: ADC1_IN10–15; PC6/7: TIM3_CH1/2 + USART6; PC14/15: OSC32 |
| PD | PD0, PD1, PD11–PD15 | PD0/1: CAN1; PD11–15: TIM4_CH1–4 (AF2) |
| PE | PE0–PE2, PE4–PE8, PE10, PE12, PE14, PE15 | PE5/6: TIM9_CH1/2; PE14: TIM1_CH4; PE8/10/12: TIM1 complementary; PE0/1: UART8; PE7/8: UART7 |

## Reference Project

Password manager logic implemented in Rust/Embassy at ~/projects/stm32f746-disc.
See src/usb_fs_host.rs, src/pm.rs, src/usb_hs.rs for the state machine to port.
