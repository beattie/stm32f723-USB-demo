# Hardware Reference — STM32F723VETx Custom Board v0.1

## SoC

| Property | Value |
|----------|-------|
| Part | STM32F723VET6 |
| Core | Cortex-M7, up to 216 MHz |
| Package | LQFP100, 0.5mm pitch |
| Flash | 512KB |
| SRAM | 256KB |
| Internal USB PHY | OTG_HS full-speed (PB14/PB15) |

## Power

| Rail | Source | Measured |
|------|--------|---------|
| VDD | USB-C → AMS1117 3.3V LDO | 3.285V |
| VDDA | VDD via ferrite bead | 3.2V |
| VCAP1 (pin 19) | Internal LDO output | 1.2V |
| VCAP2 (pin 22) | Internal LDO output | 1.2V |
| NRST (pin 14) | Pull-up | 3.3V |
| BOOT0 | Pull to GND | 0V (normal boot) |

VCAP ~1.2V confirms internal core LDO is healthy. 0V or 3.3V on VCAP indicates
a problem with VCAP bypass caps or internal regulator.

## Clock Strategy

No external crystal.
- **SYSCLK**: HSI16 (16 MHz internal RC) — used for bring-up
- **USB 48MHz**: HSI48 + CRS (Clock Recovery System auto-trims to USB SOF)
- **Production**: HSI16 → PLL for higher SYSCLK (up to 216 MHz)

## Debug

| Interface | Probe | Command |
|-----------|-------|---------|
| SWD (primary) | LPC-LINK2 CMSIS-DAP (1fc9:0090) | `--probe 1fc9:0090` |
| SWD (alternate) | STLink V2-1 (0483:374b) | `--probe 0483:374b` |
| ITM/SWO | J7 Cortex Debug connector | SWO pin on J7 |

No UART on board. Debug output via SWO/ITM or USB CDC (future).

Flash commands:
```bash
probe-rs download --chip STM32F723VETx --probe 1fc9:0090 bringup/bringup.elf
probe-rs reset --chip STM32F723VETx --probe 1fc9:0090
```

## GPIO Map

| Signal | Pin | Net | Notes |
|--------|-----|-----|-------|
| LED1 | — | Power (always on via R9 330Ω) | Not GPIO |
| LED2 | PE0 | BLUE | R19 bodge (metric footprint) |
| LED3 | PE1 | GREEN | R22 bodge (metric footprint) |
| LED4 | PE2 | YELLOW | R23 bodge (metric footprint) |
| LED5_R | PB4 | RGB RED | Not installed |
| LED5_G | PB5 | RGB GREEN | Not installed |
| LED5_B | PB6 | RGB BLUE | Not installed |
| SWDIO | PA13 | Debug | |
| SWCLK | PA14 | Debug | |
| USBA+ | PA12 (pin 71) | USB-A host D+ | OTG_FS |
| USBA- | PA11 (pin 70) | USB-A host D- | OTG_FS |
| USBC+ | PB15 (pin 57) | USB-C device D+ | OTG_HS internal FS PHY |
| USBC- | PB14 (pin 56) | USB-C device D- | OTG_HS internal FS PHY |
| MMC1_CDET | PE3 | SD card detect | R8 47K pullup, active low |
| MMC1_CLK | ? | SD card clock | |
| MMC1_CMD | ? | SD card command | |
| MMC1_D0-3 | ? | SD card data | |
| SPI1_SCK | ? | Display SCK | |
| SPI1_MOSI | ? | Display MOSI | |
| SPI1_MISO | ? | Display MISO | |
| LCD_CS | ? | Display CS | |
| LCD_DC | ? | Display D/C | |
| LCD_BL | ? | Display backlight | |
| LCD_RST | pin 69 | Disconnected | Not needed for demo |

USB-C CC pins have 5.1kΩ pull-downs (signals 5V sink to USB-C charger/host).

## Test Points

| TP | Signal |
|----|--------|
| TP12 | USBC+ |
| TP13 | USBC- |
| TP14 | USBA- |
| TP15 | USBA+ |

## v0.1 Known Issues (fix in v0.2)

### Schematic bugs

**VDDA bypass caps in series** — schematic has 1µF → 100nF → GND in series.
Should be parallel to GND. Workaround: bridge junction between caps to GND,
making 1µF the effective bypass. VDDA reads 3.2V (acceptable).

**VDDPHYHS same bug** — FB2 → 1µF → 100nF → GND. Same parallel cap fix.
FB2 + C23 + C24 installed with fix applied (2026-07-10). ✅

**LED current limiter footprints** — R_0201_0603Metric footprint used where
0603 imperial (1608 metric) needed. Bodge required for all LED resistors:
R19 (LED2), R22 (LED3), R23 (LED4).

### Board-level issues

**No UART** — use SWO/ITM via J7 for debug output, or USB CDC in firmware.

**Pin 69 (LCD_RST) disconnected** — not a problem, not needed for demo.

**LED5 (RGB, PB4/5/6) not installed** — not needed for bring-up demo.

### Silkscreen

**Pin 1 marker missing** — no clear pin 1 indicator on SoC caused 180° placement
error on earlier boards. Add prominent marker in v0.2.

## Bring-Up Log

### 2026-07-10

- Smoke test passed: SWD connects, all voltages nominal
- LED chase firmware flashed: blue LED confirmed blinking
- FB2/C23/C24 installed: VDDPHYHS filter complete
- R19/R22/R23 bodged (metric footprint issue)
- LED2 (BLUE, PE0), LED3 (GREEN, PE1), LED4 (YELLOW, PE2) all verified
- LED4: bad solder joint on PE2 (pin) found and reflowed
- PE3 = MMC1_CDET confirmed (R8 47K pullup to 3.3V)

### Notes for SD bring-up

Check PE3 reads high (3.3V / logic 1) with no card inserted. If it reads low
with no card, suspect bad joint on R8 or solder bridge on the PE3 trace.

## v0.2 Planned Fixes

- [ ] Fix VDDA and VDDPHYHS cap topology (parallel, not series)
- [ ] Fix LED current limiter footprints (0603 imperial / 1608 metric)
- [ ] Add SoC pin 1 silkscreen marker
- [ ] Consider adding UART (debug) or USB-UART bridge
