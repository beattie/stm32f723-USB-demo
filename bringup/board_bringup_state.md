# STM32F723VET6 USB Passthrough Board v0.1 — Bring-Up State

## Board Overview

Custom STM32F723VET6 LQFP100 USB passthrough board, hand-assembled. First article (v0.1). Purpose is to pass USB traffic through the STM32 for enumeration/inspection.

- SoC: STM32F723VET6, Cortex-M7, LQFP100, 0.5mm pitch
- Power: USB-C input → AMS1117 3.3V LDO
- Debug: 6-pin SWD header with 100Ω series resistors on SWDIO and SWCLK

## Current State — SMOKE TEST PASSED

SWD connection confirmed with probe-rs on kokopelli:

```
probe-rs info --chip STM32F723VETx --probe 1fc9:0090
```

Output confirms: DPv2, STMicroelectronics, Cortex-M7, ROM tables present. Board is alive.

Probe is LPC-LINK2 CMSIS-DAP (1fc9:0090). STLink V2-1 (0483:374b) also available.
JTAG errors in output are normal — probe-rs tries JTAG first then falls back to SWD.

## Populated Components (Smoke Test Minimum)

- VCAP1 (pin 19): 2.2µF to GND — reads ~1.2V (internal LDO healthy)
- VCAP2 (pin 22): 2.2µF to GND — reads ~1.2V
- VDD bypass: 100nF + 4.7µF near pin 11
- NRST (pin 14): 100nF to GND
- BOOT0: pulled to GND (normal boot mode)
- VDDA (pin 21): FB + bypass caps (see known issues below)
- SWD header: populated with 100Ω series resistors on SWDIO/SWCLK
- Power LED: installed, correct polarity
- USB-C power connector + AMS1117 regulator

## Known Issues (v0.1)

### Schematic bugs (fix in v0.2)
- **VDDA bypass caps in series**: Schematic has 1µF → 100nF → GND (series). Should be both caps in parallel to GND. Workaround applied: junction between caps bridged to GND, making 1µF the effective bypass. VDDA reads 3.2V.
- **VDDPHYHS same bug**: FB → 1µF → 100nF → GND. Same fix needed in v0.2.
- **LED current limiter footprints**: R_0201_0603Metric footprint used where 0603 imperial needed.

### Board-level issues
- ~~**Pins 69/70 shorted**~~ — RESOLVED (2026-07-09): USBA- (pin 70) confirmed good. Pin 69 (LCD_RST) is disconnected — not needed for the demo.
- **Missing ferrite bead**: VDDA/VDDPHYHS filter FB not stuffed (dropped during assembly, replacement on order: 600Ω @ 100MHz 0603). Not a blocker.

### Silkscreen
- **Pin 1 marker missing**: No clear pin 1 indicator on SoC silkscreen caused 180° placement error on earlier boards. Add to v0.2.

## Voltage Reference

| Net | Pin(s) | Expected | Measured |
|-----|--------|----------|---------|
| VDD | 11, 27, 50, 75, 100 | 3.3V | 3.285V |
| VDDA | 21 | 3.3V | 3.2V |
| VCAP1 | 19 | ~1.2V | 1.2V |
| VCAP2 | 22 | ~1.2V | 1.2V |
| NRST | 14 | 3.3V | 3.3V |
| BOOT0 | — | 0V | 0V |

## Next Steps

### 1. Flash minimal blink firmware
Verify the full flash pipeline and confirm clock tree comes up correctly.

- Toolchain: arm-none-eabi-gcc (or Rust/Embassy)
- Framework: STM32CubeF7 HAL or bare metal with CMSIS
- Target: toggle a GPIO (check schematic for available LED or test point)
- Flash with: `probe-rs download --chip STM32F723VETx --probe 1fc9:0090 firmware.elf`
- Verify with: `probe-rs run` or observe LED

### 2. Clock tree verification
STM32F723 has HSI (16MHz internal) and supports external HSE. Confirm which clock source is available on this board and configure accordingly.

### 3. USB bring-up
After blink works:
- Verify USBA+ (pin 71) and USBA- (pin 70) — pin 70 confirmed good
- USB-C CC pins have 5.1kΩ pull-downs (signals 5V sink to charger)
- Test USB enumeration on host

### 4. v0.2 fixes to make
- Fix VDDA and VDDPHYHS cap topology (parallel not series)
- Add SoC pin 1 silkscreen marker
- Fix LED current limiter footprints (0603 imperial)
- ~~Investigate and fix pins 69/70 pad issue~~ — resolved, not needed

## Tool Reference

```bash
# Probe discovery
probe-rs list

# Chip info / smoke test
probe-rs info --chip STM32F723VETx --probe 1fc9:0090

# Flash firmware
probe-rs download --chip STM32F723VETx --probe 1fc9:0090 firmware.elf

# Attach GDB server
probe-rs gdb --chip STM32F723VETx --probe 1fc9:0090
```

## Hardware Notes

- Iron reflow at 370°C with TS-1 tip and liquid no-clean flux is preferred over hot air for this SoC
- Pin 1 is bottom-left corner when text reads right-side up — small concave dot, NOT the ejector mark on the opposite corner
- VCAP pins are internal LDO outputs — ~1.2V confirms core is running; 0V or 3.3V means something is wrong
