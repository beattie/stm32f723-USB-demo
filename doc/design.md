# USB HID Passthrough + Password Manager — Device Design

> Imported and extended from `~/projects/stm32f746-disc/POC.md`

## Overview

A USB HID keyboard passthrough with integrated hardware password manager. The
device sits between a physical USB keyboard and a host computer. Keystrokes pass
through transparently during normal use. When triggered, the device injects stored
credentials (username + password) as if typed by the keyboard.

Works with any host OS (Linux CLI, GUI, web browsers, lock screens) — no drivers
or host software required. The device appears as a standard USB HID keyboard and
optionally a USB Mass Storage device (SD card as USB drive).

---

## Hardware

### Development platform: STM32F746G-DISCO

- MCU: STM32F746NGH6 (Cortex-M7, 216 MHz)
- 1MB internal flash, 256KB RAM, 8MB external SDRAM
- Display: 4.3" LCD-TFT 480×272 (optional)
- SD card via SDMMC1
- ST-Link V2 for programming/debug

USB topology (discovery board):
```
[USB Keyboard] → [CN13 OTG_FS host, 12 Mbps] → [STM32F746] → [CN12 OTG_HS device, 480 Mbps] → [Computer]
                                                       |
                                                  [SD card]
                                             (credential store)
```

### Target platform: STM32F723VETx (custom board v0.1)

- MCU: STM32F723VE (Cortex-M7, LQFP100)
- 512KB flash, 256KB SRAM — no external memory
- No external crystal: HSI16 for SYSCLK, HSI48 + CRS for USB 48MHz
- Internal OTG_HS FS PHY (no external ULPI chip needed)
- SD card via SDMMC1

USB topology (custom board):
```
[USB Keyboard] → [USB-A OTG_FS host, PA11/PA12] → [STM32F723] → [USB-C OTG_HS device, PB14/PB15] → [Computer]
                                                         |
                                                    [MicroSD]
                                               (credential store)
```

---

## Normal Use Flow

```
1. User is at a login prompt (username field focused)
2. Press Pause/Break → STM32 stops forwarding keys
3. User types app password (no visual feedback on host — looks idle to observer)
4. Enter/Tab:
   a. Correct password → proceed to SELECT
   b. Wrong password   → STM32 types "BAD PWD!" → any key erases it (8× BS) → PASSTHROUGH
5. SELECT: STM32 types current entry username into host field
6. Up/Down → STM32 erases username (backspaces) + types next/prev entry username
7. Enter → host receives Enter (username submitted, focus moves to password field)
8. WAIT: user confirms ready (Tab or Enter)
9. STM32 decrypts and types password + Enter → PASSTHROUGH
```

Notes:
- "BAD PWD!" is exactly 8 chars for clean erasure from any field
- All keys in non-PASSTHROUGH modes are consumed (not forwarded to host)
- Display (optional): show current mode and entry name on LCD

---

## Mode State Machine

```
State 0 — BAD_PASSWORD
  On entry: type "BAD PWD!" to host
  Any key  → type 8× BS → PASSTHROUGH (1)

State 1 — PASSTHROUGH  [initial state]
  Trigger (Pause/Break) → GETPASSWORD (2)
  Any key → forward to host unchanged

State 2 — GETPASSWORD
  Printable → accumulate in pw_buf (not forwarded)
  Enter/Tab → AUTHENTICATE (3)
  Escape    → discard buffer → PASSTHROUGH (1)

State 3 — AUTHENTICATE  [transient — not a stable state]
  Derive key, attempt decrypt of credential database
  Success → set entry index = 0 → SELECT (4)
  Failure → BAD_PASSWORD (0)

State 4 — SELECT
  On entry: type current entry username to host
  Enter  → forward Enter → WAIT (5)
  Up     → erase username (backspaces) + decrement index → SELECT (4)
  Down   → erase username (backspaces) + increment index → SELECT (4)
  Escape → erase username (backspaces) → PASSTHROUGH (1)

State 5 — WAIT
  Tab/Enter → forward Tab/Enter → SENDPASSWORD (6)
  Escape    → PASSTHROUGH (1)
  Any other → consumed, stay in WAIT

State 6 — SENDPASSWORD
  Decrypt entry password, type password + Enter → PASSTHROUGH (1)
```

See [state-machine.md](state-machine.md) for implementation details and the
Rust refactor design.

---

## Security Design

The password is the encryption key — same model as KeePass. The database is a
single encrypted file, fully portable. "Change password" means decrypt and
re-encrypt the whole file on a host computer; the device only ever decrypts.

### Key derivation

```
password + salt → PBKDF2-SHA256 (N iterations) → 32-byte key
key + password.enc → ChaCha20-Poly1305 decrypt → plaintext password.txt
```

The salt is random, generated at encrypt time, stored in the file header.
STM32F7 has a hardware HASH peripheral (SHA-256) to accelerate PBKDF2.

### Security properties

| Threat | Protected? |
|--------|-----------|
| Stolen SD card only | ✅ database encrypted, password required |
| Both stolen, no password | ✅ PBKDF2 makes brute force expensive |
| Changing password | ✅ decrypt + re-encrypt on host, device unchanged |
| Portability | ✅ database works on any device with the correct password |

---

## Credential Database Format

Single file `/password.txt` on SD card (plaintext for POC; `/password.enc` when encrypted).

```
Passwords
username\tpassword
username\tpassword
...
```

- First line `Passwords\n` — magic header for validation
- Tab-separated fields, newline-terminated records
- Printable ASCII only (no tabs or newlines in values)
- Maintainable with any text editor

Encrypted file layout:
```
[16 bytes random salt][12 bytes nonce][ciphertext + 16 byte Poly1305 tag]
```
Plaintext inside encryption is the same text format.

### Why not KeePass (.kdbx)?

- No `no_std` Rust parser available
- KDBX3: AES-KDF + zlib + XML — too heavy for 256KB SRAM
- KDBX4: Argon2 — memory-hard, not feasible on embedded
- Simple tab-separated format achieves the same security with far less complexity
- Future: KDBX3 may be feasible on F746 (8MB SDRAM) — worth revisiting

---

## Desktop Tool (Python, portable)

```bash
pwmgr encrypt --sd /media/SD    # encrypt plaintext database
pwmgr rekey --sd /media/SD      # change password
pwmgr list --sd /media/SD       # list entries
```

Prompts for passwords interactively (not on command line). Uses same
ChaCha20-Poly1305 + PBKDF2-SHA256 as the firmware.

---

## Implementation Phases

### Phase 1 — POC (plaintext, no crypto)
Validate UX flow end-to-end before adding encryption.
- Plain `password.txt`, no app password step
- Flow: Pause/Break → navigate → Enter → type username → Tab → type password → Enter
- Read SD card via Embassy SDMMC driver

### Phase 2 — Crypto
- PBKDF2-SHA256 using hardware HASH peripheral
- ChaCha20-Poly1305 encrypt/decrypt (`chacha20poly1305` no_std crate)
- Password intercept → derive key → decrypt `/password.enc`

### Phase 3 — Custom board
- Port to STM32F723VETx (this project)
- Re-read approach for SD: no full-file buffer (256KB SRAM constraint)
- Label, username, password each bounded (128 bytes max)

### Phase 4 — Polish
- Display: scrollable list, password entry masked
- Secure erase / factory reset
- TOTP (needs RTC or USB time sync)
- FIDO2 / WebAuthn

---

## Implementation Notes

- HID keycode conversion: all printable ASCII must map to HID keycodes
  (accounting for Shift for uppercase/symbols)
- Credential injection timing: some apps need inter-keystroke delays —
  configurable per-entry or globally
- F723 constraint: re-read file on each Up/Down rather than buffering whole DB
  (Embassy sector cache makes this cheap)
- `chacha20poly1305` crate works in `no_std` without `alloc`
