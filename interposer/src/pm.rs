use core::num::NonZeroU8;

// HID keycodes
const HID_SCROLL_LOCK: u8 = 0x47;
const HID_UP:          u8 = 0x52;
const HID_DOWN:        u8 = 0x51;
const HID_ENTER:       u8 = 0x28;
const HID_TAB:         u8 = 0x2B;
pub const HID_BS:      u8 = 0x2A;   // backspace — used by kbd_task for erase
pub const HID_SPACE:   u8 = 0x2C;   // space     — used by kbd_task for erase

// Left Shift modifier byte
const MOD_LSHIFT: u8 = 0x02;

// ── Credentials ──────────────────────────────────────────────────────────────

pub struct Credential {
    pub display:  &'static str,  // shown on device display
    pub send:     &'static str,  // typed to host ("" = skip label step)
    pub username: &'static str,
    pub password: &'static str,
}

pub const CREDENTIALS: &[Credential] = &[
    Credential {
        display:  "github.com",
        send:     "github.com",
        username: "beattie",
        password: "test1234",
    },
    Credential {
        display:  "gmail.com",
        send:     "gmail.com",
        username: "beattie@gmail.com",
        password: "test5678",
    },
    Credential {
        display:  "no-label site",
        send:     "",
        username: "bob",
        password: "secret",
    },
];

// ── Mode ─────────────────────────────────────────────────────────────────────

#[derive(Clone, Copy, PartialEq)]
pub enum Mode {
    Passthrough,
    PmSelect(usize),    // cursor index into CREDENTIALS
    PmTypeUser(usize),  // cursor index, username typed to host
    PmTypePass(usize),  // cursor index, waiting for user to confirm send
}

impl Mode {
    pub fn label(self) -> &'static str {
        match self {
            Mode::Passthrough    => "PASSTHROUGH ",
            Mode::PmSelect(_)    => "PM: SELECT  ",
            Mode::PmTypeUser(_)  => "PM: USERNAME",
            Mode::PmTypePass(_)  => "PM: PASSWORD",
        }
    }
}

// ── PmEffect ─────────────────────────────────────────────────────────────────

/// Returned by handle_key; kbd_task executes fields in order:
///   forward → (or) erase → text → send_key → disp28 → disp36
pub struct PmEffect {
    pub forward:  bool,                  // pass received HID report to host unchanged
    pub erase:    usize,                 // BS-SPC-BS × n to erase chars from host field
    pub text:     &'static str,          // then type this text character by character
    pub send_key: Option<u8>,            // then send this HID keycode (Tab or Enter)
    pub disp28:   Option<&'static str>,  // update row 28 (mode label), None = no change
    pub disp36:   Option<&'static str>,  // update row 36 (credential name), None = no change
}

impl PmEffect {
    fn forward() -> Self {
        Self { forward: true,  erase: 0, text: "", send_key: None, disp28: None, disp36: None }
    }
    fn swallow() -> Self {
        Self { forward: false, erase: 0, text: "", send_key: None, disp28: None, disp36: None }
    }
}

// ── PmState ──────────────────────────────────────────────────────────────────

pub struct PmState {
    pub mode: Mode,
}

impl PmState {
    pub fn new() -> Self {
        Self { mode: Mode::Passthrough }
    }

    pub fn handle_key(&mut self, modifiers: u8,
                      keys: &[Option<NonZeroU8>]) -> PmEffect {
        let _ = modifiers;
        let key     = |code: u8| keys.iter().any(|k| k.map_or(false, |v| v.get() == code));
        let tab     = key(HID_TAB);
        let advance = tab || key(HID_ENTER);
        let send_key = if tab { HID_TAB } else { HID_ENTER };

        match self.mode {

            // ── PASSTHROUGH ──────────────────────────────────────────────────
            Mode::Passthrough => {
                if key(HID_SCROLL_LOCK) && !CREDENTIALS.is_empty() {
                    self.mode = Mode::PmSelect(0);
                    return PmEffect {
                        forward:  false,
                        erase:    0,
                        text:     CREDENTIALS[0].send,
                        send_key: None,
                        disp28:   Some(self.mode.label()),
                        disp36:   Some(CREDENTIALS[0].display),
                    };
                }
                PmEffect::forward()
            }

            // ── PM_SELECT ────────────────────────────────────────────────────
            // site label is live in the host's username field while navigating
            Mode::PmSelect(cursor) => {
                if key(HID_SCROLL_LOCK) {
                    // Cancel: erase site label, return to Passthrough
                    let erase = CREDENTIALS[cursor].send.len();
                    self.mode = Mode::Passthrough;
                    return PmEffect {
                        forward: false, erase, text: "",
                        send_key: None,
                        disp28: Some(self.mode.label()),
                        disp36: Some(""),
                    };
                }
                if key(HID_UP) && cursor > 0 {
                    let erase = CREDENTIALS[cursor].send.len();
                    let c = cursor - 1;
                    self.mode = Mode::PmSelect(c);
                    return PmEffect {
                        forward: false, erase, text: CREDENTIALS[c].send,
                        send_key: None,
                        disp28: Some(self.mode.label()),
                        disp36: Some(CREDENTIALS[c].display),
                    };
                }
                if key(HID_DOWN) && cursor + 1 < CREDENTIALS.len() {
                    let erase = CREDENTIALS[cursor].send.len();
                    let c = cursor + 1;
                    self.mode = Mode::PmSelect(c);
                    return PmEffect {
                        forward: false, erase, text: CREDENTIALS[c].send,
                        send_key: None,
                        disp28: Some(self.mode.label()),
                        disp36: Some(CREDENTIALS[c].display),
                    };
                }
                if advance {
                    // Select: erase site label, type username
                    let erase = CREDENTIALS[cursor].send.len();
                    self.mode = Mode::PmTypeUser(cursor);
                    return PmEffect {
                        forward: false, erase,
                        text:     CREDENTIALS[cursor].username,
                        send_key: None,
                        disp28:   Some(self.mode.label()),
                        disp36:   Some(CREDENTIALS[cursor].display),
                    };
                }
                PmEffect::swallow()
            }

            // ── PM_TYPE_USER ─────────────────────────────────────────────────
            // username is live in the host's username field
            Mode::PmTypeUser(cursor) => {
                if key(HID_SCROLL_LOCK) {
                    // Cancel: erase username, return to Passthrough
                    let erase = CREDENTIALS[cursor].username.len();
                    self.mode = Mode::Passthrough;
                    return PmEffect {
                        forward: false, erase, text: "",
                        send_key: None,
                        disp28: Some(self.mode.label()),
                        disp36: Some(""),
                    };
                }
                if key(HID_UP) && cursor > 0 {
                    let erase = CREDENTIALS[cursor].username.len();
                    let c = cursor - 1;
                    self.mode = Mode::PmSelect(c);
                    return PmEffect {
                        forward: false, erase, text: CREDENTIALS[c].send,
                        send_key: None,
                        disp28: Some(self.mode.label()),
                        disp36: Some(CREDENTIALS[c].display),
                    };
                }
                if key(HID_DOWN) && cursor + 1 < CREDENTIALS.len() {
                    let erase = CREDENTIALS[cursor].username.len();
                    let c = cursor + 1;
                    self.mode = Mode::PmSelect(c);
                    return PmEffect {
                        forward: false, erase, text: CREDENTIALS[c].send,
                        send_key: None,
                        disp28: Some(self.mode.label()),
                        disp36: Some(CREDENTIALS[c].display),
                    };
                }
                if advance {
                    // Advance: send Tab/Enter to move host to password field
                    self.mode = Mode::PmTypePass(cursor);
                    return PmEffect {
                        forward: false, erase: 0, text: "",
                        send_key: Some(send_key),
                        disp28: Some(self.mode.label()),
                        disp36: Some(CREDENTIALS[cursor].display),
                    };
                }
                PmEffect::swallow()
            }

            // ── PM_TYPE_PASS ─────────────────────────────────────────────────
            // host is in the password field; user confirms before password is sent
            Mode::PmTypePass(cursor) => {
                if key(HID_SCROLL_LOCK) {
                    // Cancel: don't erase — avoid revealing password length
                    self.mode = Mode::Passthrough;
                    return PmEffect {
                        forward: false, erase: 0, text: "",
                        send_key: None,
                        disp28: Some(self.mode.label()),
                        disp36: Some(""),
                    };
                }
                if advance {
                    // Confirm: type password then send Tab/Enter
                    self.mode = Mode::Passthrough;
                    return PmEffect {
                        forward: false, erase: 0,
                        text:     CREDENTIALS[cursor].password,
                        send_key: Some(send_key),
                        disp28:   Some(self.mode.label()),
                        disp36:   Some(""),
                    };
                }
                PmEffect::swallow()
            }
        }
    }
}

// ── Character → HID keycode ──────────────────────────────────────────────────

/// Returns (keycode, modifier) for a printable ASCII character.
/// modifier 0x02 = Left Shift.  Returns None for unsupported characters.
pub fn char_to_hid(c: char) -> Option<(u8, u8)> {
    match c {
        'a'..='z' => Some((0x04 + c as u8 - b'a', 0x00)),
        'A'..='Z' => Some((0x04 + c as u8 - b'A', MOD_LSHIFT)),
        '1' => Some((0x1E, 0x00)), '!' => Some((0x1E, MOD_LSHIFT)),
        '2' => Some((0x1F, 0x00)), '@' => Some((0x1F, MOD_LSHIFT)),
        '3' => Some((0x20, 0x00)), '#' => Some((0x20, MOD_LSHIFT)),
        '4' => Some((0x21, 0x00)), '$' => Some((0x21, MOD_LSHIFT)),
        '5' => Some((0x22, 0x00)), '%' => Some((0x22, MOD_LSHIFT)),
        '6' => Some((0x23, 0x00)), '^' => Some((0x23, MOD_LSHIFT)),
        '7' => Some((0x24, 0x00)), '&' => Some((0x24, MOD_LSHIFT)),
        '8' => Some((0x25, 0x00)), '*' => Some((0x25, MOD_LSHIFT)),
        '9' => Some((0x26, 0x00)), '(' => Some((0x26, MOD_LSHIFT)),
        '0' => Some((0x27, 0x00)), ')' => Some((0x27, MOD_LSHIFT)),
        ' ' => Some((0x2C, 0x00)),
        '-' => Some((0x2D, 0x00)), '_'  => Some((0x2D, MOD_LSHIFT)),
        '=' => Some((0x2E, 0x00)), '+'  => Some((0x2E, MOD_LSHIFT)),
        '[' => Some((0x2F, 0x00)), '{'  => Some((0x2F, MOD_LSHIFT)),
        ']' => Some((0x30, 0x00)), '}'  => Some((0x30, MOD_LSHIFT)),
        ';' => Some((0x33, 0x00)), ':'  => Some((0x33, MOD_LSHIFT)),
       '\'' => Some((0x34, 0x00)), '"'  => Some((0x34, MOD_LSHIFT)),
        '`' => Some((0x35, 0x00)), '~'  => Some((0x35, MOD_LSHIFT)),
        ',' => Some((0x36, 0x00)), '<'  => Some((0x36, MOD_LSHIFT)),
        '.' => Some((0x37, 0x00)), '>'  => Some((0x37, MOD_LSHIFT)),
        '/' => Some((0x38, 0x00)), '?'  => Some((0x38, MOD_LSHIFT)),
       '\\' => Some((0x31, 0x00)), '|'  => Some((0x31, MOD_LSHIFT)),
        _   => None,
    }
}
