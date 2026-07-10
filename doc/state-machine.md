# Password Manager State Machine — Implementation Design

> Imported and extended from `~/projects/stm32f746-disc/REFACTOR.md`

## Overview

The state machine runs inside `keyboard_poll_task` in the Embassy firmware.
It consumes HID reports from the USB host (physical keyboard) and produces
actions that the task executes: forwarding keypresses, typing credentials,
reading the SD card.

The key design principle is **separation of concerns**:
- `state_machine()` is a pure function — no I/O, no async, no file access
- The task owns all resources (SD card, filesystem, buffers)
- The state machine returns actions; the task executes them

---

## Rust Types

```rust
// pm.rs

#[derive(PartialEq)]
enum PmAction {
    Null,           // no external action needed
    Authenticate,   // open file, verify pw_buf, call state_machine with result
    NextEntry,      // erase username, increment index, seek+read entry, type username
    PrevEntry,      // erase username, decrement index, seek+read entry, type username
    SendPassword,   // seek to user_index entry, type password + Enter
}

// Mode constants (u8 for AtomicU8 sharing with display task)
const MODE_BAD_PASSWORD: u8 = 0;
const MODE_PASSTHROUGH:  u8 = 1;  // initial state
const MODE_GETPASSWORD:  u8 = 2;
const MODE_AUTHENTICATE: u8 = 3;  // transient
const MODE_SELECT:       u8 = 4;
const MODE_WAIT:         u8 = 5;
const MODE_SENDPASSWORD: u8 = 6;
```

---

## State Machine Signature

```rust
fn state_machine(
    report: [u8; 8],        // incoming HID report from keyboard
    mode: u8,               // current mode
    auth_result: bool,      // true normally; false → BAD_PASSWORD in MODE_AUTHENTICATE
    pw_buf: &mut [u8; 64],  // accumulated password buffer
    pw_len: &mut usize,     // valid bytes in pw_buf
    user_index: &mut usize, // current credential index
) -> (u8, PmAction)         // (new_mode, action_for_task)
```

Not async — no file I/O, no `type_*` calls. The task handles all side effects.

---

## Task Loop

```rust
// usb_fs_host.rs — keyboard_poll_task

loop {
    let report = poll_keyboard(&mut dpid).await;

    if report != prev_report {
        let (mut mode, action) = state_machine(report, mode, true, &mut pw_buf, &mut pw_len, &mut user_index);

        match action {
            PmAction::Authenticate => {
                let ok = authenticate(&pw_buf[..*pw_len], &mut fs).await;
                (mode, _) = state_machine(report, mode, ok, &mut pw_buf, &mut pw_len, &mut user_index);
            }
            PmAction::NextEntry => {
                *user_index = user_index.saturating_add(1);
                erase_username(&username[..*username_len]).await;
                (*username_len, *username) = read_entry_username(*user_index, &mut fs).await;
                type_string(&username[..*username_len]).await;
            }
            PmAction::PrevEntry => {
                if *user_index > 0 { *user_index -= 1; }
                erase_username(&username[..*username_len]).await;
                (*username_len, *username) = read_entry_username(*user_index, &mut fs).await;
                type_string(&username[..*username_len]).await;
            }
            PmAction::SendPassword => {
                let pwd = read_entry_password(*user_index, &mut fs).await;
                type_string(&pwd).await;
                type_key(HID_ENTER).await;
            }
            PmAction::Null => {}
        }

        PM_MODE.store(mode, Ordering::Relaxed);
        prev_report = report;
    }

    Timer::after_millis(10).await;
}
```

---

## Task Ownership

`keyboard_poll_task` owns all mutable state:

```rust
let mut sd_guard: Option<MutexGuard<SdCard>> = None;  // taken at trigger, released at PASSTHROUGH
let mut fs: Option<FatFs<SdStream>> = None;            // opened on authenticate success
let mut user_index: usize = 0;
let mut username: [u8; 128] = [0; 128];
let mut username_len: usize = 0;
let mut pw_buf: [u8; 64] = [0; 64];
let mut pw_len: usize = 0;
```

SD mutex is taken when entering GETPASSWORD (trigger) and released when
returning to PASSTHROUGH. This prevents the MSC task from accessing the card
while the password manager is active.

---

## Authentication

```rust
async fn authenticate(pw: &[u8], fs: &mut Option<FatFs<SdStream>>) -> bool {
    // Open /password.txt (plaintext POC) or /password.enc (Phase 2)
    // Phase 1: check magic header "Passwords\n"
    // Phase 2: PBKDF2-SHA256(pw, salt) → key → ChaCha20-Poly1305 decrypt → check header
    ...
}
```

Phase 2 key derivation:
```
pw + salt (from file header) → PBKDF2-SHA256 (hardware HASH peripheral) → 32-byte key
key + nonce → ChaCha20-Poly1305 decrypt → check "Passwords\n" header
```

---

## HID Key Mapping

All printable ASCII must be converted to HID keycodes. Key points:
- Lowercase a-z: keycodes 0x04–0x1D (no modifier)
- Uppercase A-Z: same keycodes + Left Shift modifier
- Digits 0-9: 0x27 (0), 0x1E–0x26 (1–9)
- Symbols: keycodes vary, many require Shift — full table needed
- Special: Enter=0x28, Tab=0x2B, Backspace=0x2A, Escape=0x29, Pause=0x48

HID boot-protocol report format (8 bytes):
```
[0] modifier (0x02=LShift, 0x00=none)
[1] reserved (0x00)
[2] keycode1
[3] keycode2
[4] keycode3
[5] keycode4
[6] keycode5
[7] keycode6
```

Between each key: send a zero report (key-up) to avoid auto-repeat.

---

## Credential File Re-Read Strategy (F723 — 256KB SRAM)

No full-file buffer. On each Next/Prev entry:
1. Seek to file start
2. Skip header line
3. Skip `user_index` lines
4. Read one line → parse username\tpassword

Embassy's FAT sector cache makes repeated seeks cheap.
Entry bounds: 128 bytes each for label, username, password.

---

## Reference Implementation

Source files in `~/projects/stm32f746-disc/firmware/src/`:

| File | Contents |
|------|----------|
| `pm.rs` | MODE_* constants, KEY_* constants, HID keycode table |
| `usb_fs_host.rs` | `keyboard_poll_task`, `poll_keyboard`, `authenticate`, `type_*` helpers |
| `usb_hs.rs` | USB HS device (HID keyboard + MSC composite) |
| `usb.rs` | Shared USB utilities |
