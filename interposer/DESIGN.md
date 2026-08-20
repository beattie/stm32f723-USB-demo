# Interposer Secure Storage Device — Design Draft

## Overview

A USB interposer device that sits between a keyboard and a host computer. It presents as a composite USB device: a HID keyboard (passthrough) and a mass storage device (SD card). The device provides two security services:

1. **Password manager** — stores credentials encrypted on SD; types them via HID on user request
2. **Secure file store** — stores encrypted files on SD; decrypts and transfers content to host via HID typing or a temporary plaintext staging file

The host requires no special software. All security operations are performed on the device using the passthrough keyboard for input and the ST7735 display for UI.

---

## Threat Model

**Protected against:**
- Theft of SD card alone — all sensitive content is encrypted
- Theft of device alone — requires user passphrase to derive key
- Brute force of passphrase — KDF is intentionally slow; device secret adds a second factor

**Not protected against:**
- Physical possession of device AND knowledge of passphrase
- Host keyloggers (HID typing path exposes plaintext to a compromised host)
- Destruction or corruption of SD card contents

**Out of scope:**
- Protection against a sophisticated attacker with lab equipment and the physical device
- Forward secrecy
- Multi-user access

---

## Hardware

- **SoC**: STM32F723VET6, Cortex-M7, 216MHz
- **Storage**: Full-size SD card via SDMMC1 (1-bit mode, 4-bit pending)
- **Display**: ST7735 1.44" 128×128 SPI
- **USB device**: OTG_HS (USB-C), composite HID keyboard + MSC
- **USB host**: OTG_FS (USB-A), HID keyboard input
- **TRNG**: STM32 hardware RNG — used for salt/nonce/key generation
- **Unique ID**: STM32 96-bit factory UID at 0x1FF0F420 — part of device secret

---

## Key Derivation

```
device_secret = flash_secret || uid_96bit
encryption_key = Argon2id(
    passphrase  = user_passphrase,
    salt        = device_secret,
    m = 65536,          // 64MB memory — adjust for RAM constraints
    t = 3,              // iterations
    p = 1               // parallelism
)
```

**flash_secret**: 32 bytes of TRNG-generated random data, written to a dedicated flash sector during first-boot provisioning. Never exposed over any interface. Protected by STM32 RDP Level 1 when device is sealed.

**uid_96bit**: Factory-programmed, read-only. Ensures encryption_key is device-specific even if flash_secret is somehow compromised.

**Note**: STM32F723 has 256KB RAM. Argon2id memory parameter must fit — 64MB does not. Realistic target is 128–256KB. This weakens brute-force resistance; compensate with a stronger passphrase.

---

## Flash Layout

```
Sector 0–N-1   Firmware
Sector N        Device secret (32 bytes TRNG, written once at provisioning)
                [ remainder of sector unused ]
```

Sector N chosen to avoid firmware overlap. At RDP Level 1, entire flash is unreadable externally. At RDP Level 2, device is permanently sealed.

---

## SD Card Layout

Ordinary FAT32 filesystem, mountable on any host. Sensitive files use the `.sec` extension — opaque encrypted blobs to any host without the key.

```
/
├── password.sec       — encrypted password database
├── README.txt         — plaintext, explains the device to a curious host user
└── files/
    └── *.sec          — user encrypted files
```

The host can freely copy, move, or delete `.sec` files. The device manages their contents.

---

## Encrypted Object Format

```
Offset  Size  Field
0       4     Magic: "ISEC"
4       1     Version: 0x01
5       1     Type: 0x01=password_db, 0x02=text_file, 0x03=binary_file
6       2     Reserved
8       12    Nonce (TRNG-generated per encryption)
20      4     Plaintext length
24      N     ChaCha20-Poly1305 ciphertext + 16-byte tag
```

**Cipher**: ChaCha20-Poly1305 (AEAD) — software implementation via RustCrypto, no hardware CRYP needed. Provides confidentiality + integrity; a tampered file will fail authentication.

---

## Password Database Format (plaintext, before encryption)

```
IPMGR\n                     — magic header
username\tpassword\n        — one entry per line, tab-separated
username\tpassword\n
...
```

Simple, human-readable, editable with a text editor before encryption. After encryption stored as `password.sec`.

---

## Device Modes / State Machine

```
LOCKED
  │  user types passphrase on keyboard
  ▼
UNLOCKED
  ├── PASSTHROUGH   — keystrokes forwarded to host normally
  ├── PM_SELECT     — display shows credential list; Up/Down selects
  ├── PM_TYPE_USER  — device types username via HID
  ├── PM_WAIT       — waiting for user to press Enter/Tab to type password
  └── FILE_SELECT   — display shows file list; select to decrypt+transfer
```

**Trigger**: a key combination (e.g. Scroll Lock, or a configurable combo) switches from PASSTHROUGH to PM_SELECT while unlocked.

**Lock**: device re-locks after configurable idle timeout, or on explicit lock key combo. Derived key is zeroed from RAM.

---

## Transfer Mechanisms

**Password typing (PM path):**
1. Device decrypts `password.sec` in RAM
2. User selects entry on display
3. On Enter: device types username via HID
4. On next Enter: device types password via HID
5. Plaintext held in RAM only; zeroed after use

**Secure file transfer:**
1. User selects `.sec` file on display
2. Device decrypts to RAM (size limited by available RAM — ~200KB usable)
3. Device creates `PLAIN.TXT` on FAT, writes plaintext
4. Host reads file normally (no special software)
5. After timeout (e.g. 60s) or explicit lock: device overwrites `PLAIN.TXT` with zeros, deletes it
6. Display shows countdown

**Encrypt a host-written file:**
1. Host writes plaintext file to FAT (e.g. `newfile.txt`)
2. User selects it on device display, confirms
3. Device reads plaintext, encrypts, writes `newfile.sec`, deletes `newfile.txt`

---

## Open Questions

1. **Argon2id memory parameter** — 256KB RAM leaves very little headroom after firmware stack/data. May need PBKDF2 instead, with a stronger passphrase requirement.
2. **Key caching** — re-derive key on each operation, or cache in RAM for the session? Caching is faster but key lives in RAM longer.
3. **Passphrase entry UX** — display shows character count only (no echo), or masked echo (dots)? Confirmation by retyping?
4. **File size limit** — decrypt-to-RAM limits secure files to ~150–200KB. Larger files need streaming decrypt, which is more complex.
5. **Staging file timing** — fixed timeout for `PLAIN.TXT` exposure, or user-initiated wipe?
6. **Provisioning UX** — first boot generates and writes flash_secret automatically, or requires an explicit provisioning step?
7. **RDP sealing** — manual step, or triggered from device UI?
