use core::num::NonZeroU8;

const HID_SCROLL_LOCK: u8 = 0x47;

#[derive(Clone, Copy, PartialEq)]
pub enum Mode {
    Passthrough,
    PmSelect,
    // Locked added when crypto is ready
}

impl Mode {
    pub fn label(self) -> &'static str {
        match self {
            Mode::Passthrough => "PASSTHROUGH",
            Mode::PmSelect    => "PM: SELECT ",
        }
    }
}

pub enum PmAction {
    Passthrough,
    Swallow,
    UpdateDisplay,
}

pub struct PmState {
    pub mode: Mode,
}

impl PmState {
    pub fn new() -> Self {
        Self { mode: Mode::Passthrough }
    }

    pub fn handle_key(&mut self, modifiers: u8,
                      keys: &[Option<NonZeroU8>]) -> PmAction {
        let _ = modifiers;
        let scroll_lock = keys.iter()
            .any(|k| k.map_or(false, |v| v.get() == HID_SCROLL_LOCK));

        if scroll_lock {
            self.mode = match self.mode {
                Mode::Passthrough => Mode::PmSelect,
                Mode::PmSelect    => Mode::Passthrough,
            };
            return PmAction::UpdateDisplay;
        }

        match self.mode {
            Mode::Passthrough => PmAction::Passthrough,
            _                 => PmAction::Swallow,
        }
    }
}
