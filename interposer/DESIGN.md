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

INI-style, section-per-entry. Human-readable and editable with any text editor before encryption.

```ini
IPMGR

[github.com]
username = beattie
password = Kx7!mP2#qL
comment = work account

[gmail.com]
username = beattie@gmail.com
password = correct-horse-battery
```

**Rules:**
- First line is the magic header `IPMGR`
- Each entry is a `[section]` where the section name is the site label shown on the display
- Fields are `key = value` — order within a section does not matter
- Blank lines between entries are ignored
- Unknown fields are preserved on re-write (forward compatibility)
- `comment` field is optional — shown on display, not typed to host

**Known fields:** `username`, `password`, `comment`

**Open question:** encrypt the whole file as one blob, or encrypt only the password fields and keep structure plaintext. Whole-file is simpler and leaks no metadata; per-field allows browsing entry names without the key. Decision deferred.

After encryption stored as `password.sec`.

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

## Password Entry & Generation

### Manual entry

New credentials entered on-device using the passthrough keyboard:

1. User triggers "new entry" mode from PM_SELECT
2. Display shows `Site:` — user types site/username, presses Enter
3. Display shows `Password:` — user types password, OR presses a key to generate
4. Device appends entry to `password.sec` (decrypt → append → re-encrypt)

### Random generation

- **Source**: STM32 hardware TRNG — already present, used for nonces
- **Character set**: `a-z A-Z 0-9 !@#$%^&*-_+=` (no ambiguous chars like `0O1lI`)
- **Default length**: 20 characters; configurable 12–32
- **UX**: generated password shown on display before committing — user can confirm or regenerate
- **On confirm**: entry stored in `password.sec` AND typed to host via HID so the site receives it immediately

Word-based (diceware) passwords not implemented: wordlist (~80KB) would need to live on SD card, adds complexity, and many sites reject passphrases.

### Implementation dependency

Both manual entry and random generation require a **text input loop**: reading keystrokes from the passthrough keyboard (already captured in `kbd_task`) and echoing masked input on the display. This is the foundation all password manager UI builds on.

---

## Future Features

### FIDO2 / WebAuthn

A FIDO2 token eliminates the need to type passwords entirely for supported sites. The site sends a challenge, the device signs it with a per-site private key, the site verifies. The secret never leaves the device — immune to host keyloggers and phishing, stronger than HID typing for authentication.

**What's needed:**
- CTAP2 protocol over USB HID (usage page 0xF1D0) — a separate HID interface alongside the keyboard HID; embassy-usb supports multiple HID interfaces
- Per-site EC key pairs (P-256 or Ed25519), generated on device, stored encrypted on SD
- User presence confirmation — button press or PIN; display can show which site is requesting
- Resident keys (discoverable credentials) for passwordless login
- Spec: FIDO Alliance CTAP2 (open); reference implementation: SoloKeys (open source)

**Fit with this hardware:**
- Display is a genuine advantage over most FIDO2 tokens — can show the relying party ID before confirming
- RustCrypto has P-256 (`p256`) and Ed25519 (`ed25519-dalek`)
- Flash estimate: ~80KB additional

**Priority:** High — clean security model, open spec, good open source reference, doesn't require host software once registered.

---

### Monero Support

Monero's view key / spend key separation is well-suited to a hardware wallet: share the view key with a watch-only wallet to monitor incoming transactions, keep the spend key on the device.

**Realistic subset (feasible on this hardware):**
- Seed storage (Monero 25-word or BIP39 format) — encrypted on SD, same key derivation as secure storage
- View key export — derive and display/type the view key for import into Feather Wallet or Monero CLI
- Ed25519-Monero signing for simple transactions
- Host interface: Trezor protocol (Feather Wallet already supports it) or custom

**What's probably out of reach:**
- Full RingCT / Bulletproofs proving — computationally and memory intensive; Bulletproofs require significant RAM for the proof construction
- Full privacy-preserving transaction signing on-device

**What's needed:**
- Ed25519-Monero curve arithmetic (a variant of standard Ed25519) — RustCrypto `curve25519-dalek` is a starting point
- Monero key derivation (Keccak-256, not SHA-3) — RustCrypto `tiny-keccak`
- Companion software on host required (Feather Wallet or Monero CLI with hardware wallet support)
- Reference: Trezor Monero implementation (open source)

**Flash estimate:** ~100KB for Ed25519-Monero + key derivation + Trezor protocol subset.

**Priority:** Medium — personally useful (eris Monero node), simpler subset is feasible, full RingCT is not.

---

### Code Space Reality Check (STM32F723: 512KB flash, 256KB RAM)

| Component | Flash estimate |
|-----------|---------------|
| Current firmware (USB host+device, MSC, display, SD) | ~150KB |
| Secure storage (ChaCha20-Poly1305, Argon2id, FAT) | ~100KB |
| FIDO2 (CTAP2, P-256/Ed25519, key storage) | ~80KB |
| Monero subset (Ed25519-Monero, seed, view key) | ~100KB |
| **All four** | **~430KB** — tight but possibly feasible |

RAM is the harder constraint. Argon2id memory parameter and crypto buffers compete with USB stack and task stacks. Measure actual usage after core secure storage is implemented before committing to FIDO2 + Monero simultaneously.

---

## Open Questions

1. **Argon2id memory parameter** — 256KB RAM leaves very little headroom after firmware stack/data. May need PBKDF2 instead, with a stronger passphrase requirement.
2. **Key caching** — re-derive key on each operation, or cache in RAM for the session? Caching is faster but key lives in RAM longer.
3. **Passphrase entry UX** — display shows character count only (no echo), or masked echo (dots)? Confirmation by retyping?
4. **File size limit** — decrypt-to-RAM limits secure files to ~150–200KB. Larger files need streaming decrypt, which is more complex.
5. **Staging file timing** — fixed timeout for `PLAIN.TXT` exposure, or user-initiated wipe?
6. **Provisioning UX** — first boot generates and writes flash_secret automatically, or requires an explicit provisioning step?
7. **RDP sealing** — manual step, or triggered from device UI?
