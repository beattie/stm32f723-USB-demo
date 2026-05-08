# STM32F723 USB Passthrough Demo

A hardware and firmware demonstration platform based on the STM32F723VET6, a Cortex-M7
with an embedded USB HS PHY. Inspired by the [Pastilda](https://github.com/nickcoutsos/pastilda)
project, with a goal of providing robust Linux support for USB keyboard passthrough.

## Concept

The board sits between a USB keyboard and a Linux host. The firmware intercepts keystrokes,
allowing for key remapping, macros, and other HID transformations entirely in hardware —
no kernel modules or userspace daemons required on the host.

## Hardware

Designed in KiCad. The STM32F723 was chosen for its embedded USB HS PHY, which allows
full-speed USB HS without an external PHY chip.

**Key interfaces:**
- USB-C device port to host (USB 2.0 HS)
- USB-A host port for keyboard (USB FS)
- USB-C power input
- MicroSD — for config and macro storage
- SPI LCD connector — optional display
- SWD debug header

**Hardware versions:**
- Rev 0.1 — 2-layer board, initial bring-up
- Rev 0.2 — 4-layer board (F.Cu / GND / 3V3 / B.Cu), revised layout (in progress)

PCB gerbers for Rev 0.1 are in `kicad/gerbers/`.

## Firmware

Written in Rust using the [Embassy](https://embassy.dev/) async embedded framework.

```
src/main.rs      — application entry point
Cargo.toml       — dependencies
memory.x         — linker memory map for STM32F723VE
```

**Build:**
```bash
cargo build --release
```

Requires the `thumbv7em-none-eabihf` target:
```bash
rustup target add thumbv7em-none-eabihf
```

**Flash:**
```bash
cargo flash --release --chip STM32F723VETx
```
Or use OpenOCD/probe-rs via the SWD header.

## Bill of Materials

See [BOM.md](BOM.md) for the full parts list with Digikey part numbers.

## License

Hardware and firmware are released under CERN-OHL-S v2 / GPL v3 respectively.
