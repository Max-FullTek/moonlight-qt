//! Client control-page navigation shared by keyboard and controller input.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ControlAction {
    Resume,
    GamepadMouse,
    Disconnect,
    End,
    Stats,
    Fullscreen,
    MouseMode,
    Cursor,
    Paste,
    PointerLock,
    BitrateDown,
    BitrateUp,
}
impl ControlAction {
    pub const ALL: [Self; 12] = [
        Self::Resume,
        Self::GamepadMouse,
        Self::Disconnect,
        Self::End,
        Self::Stats,
        Self::Fullscreen,
        Self::MouseMode,
        Self::Cursor,
        Self::Paste,
        Self::PointerLock,
        Self::BitrateDown,
        Self::BitrateUp,
    ];
    pub fn command(self) -> &'static str {
        match self {
            Self::Resume => "resume",
            Self::GamepadMouse => "gamepad_mouse",
            Self::Disconnect => "disconnect",
            Self::End => "end",
            Self::Stats => "stats",
            Self::Fullscreen => "fullscreen",
            Self::MouseMode => "mouse_mode",
            Self::Cursor => "cursor",
            Self::Paste => "paste",
            Self::PointerLock => "pointer_lock",
            Self::BitrateDown | Self::BitrateUp => "bitrate",
        }
    }
}
#[derive(Default)]
pub struct Controls {
    pub selected: usize,
    pub confirm_end: bool,
    pub gamepad_mouse: bool,
    pub has_gamepad: bool,
    pub active: bool,
    pub bitrate: u32,
}
impl Controls {
    pub fn move_selection(&mut self, down: bool) {
        self.selected = (self.selected
            + if down {
                1
            } else {
                ControlAction::ALL.len() - 1
            })
            % ControlAction::ALL.len();
        self.confirm_end = false;
    }
    pub fn choose(&mut self, action: ControlAction) -> bool {
        if action != ControlAction::End {
            self.confirm_end = false;
        }
        if !self.active || (action == ControlAction::GamepadMouse && !self.has_gamepad) {
            return false;
        }
        if action == ControlAction::End && !self.confirm_end {
            self.selected = 3;
            self.confirm_end = true;
            return false;
        }
        self.confirm_end = false;
        true
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn end_needs_confirmation_and_navigation_cancels_it() {
        let mut controls = Controls {
            active: true,
            ..Default::default()
        };
        assert!(!controls.choose(ControlAction::End));
        assert!(controls.confirm_end);
        controls.move_selection(false);
        assert!(!controls.confirm_end);
        assert_eq!(
            ControlAction::ALL[controls.selected],
            ControlAction::Disconnect
        );
        assert!(controls.choose(ControlAction::Disconnect));
        assert!(!controls.choose(ControlAction::End));
        assert!(!controls.choose(ControlAction::GamepadMouse));
        assert!(!controls.confirm_end);
        assert!(!controls.choose(ControlAction::End));
        assert!(controls.choose(ControlAction::End));
        assert_eq!(ControlAction::Disconnect.command(), "disconnect");
        assert_eq!(ControlAction::End.command(), "end");
    }
    #[test]
    fn navigation_wraps_and_mouse_requires_a_connected_controller() {
        let mut controls = Controls::default();
        assert!(!controls.choose(ControlAction::Resume));
        controls.active = true;
        controls.move_selection(false);
        assert_eq!(controls.selected, ControlAction::ALL.len() - 1);
        controls.move_selection(true);
        assert_eq!(controls.selected, 0);
        assert!(!controls.choose(ControlAction::GamepadMouse));
        controls.has_gamepad = true;
        assert!(controls.choose(ControlAction::GamepadMouse));
    }
}
