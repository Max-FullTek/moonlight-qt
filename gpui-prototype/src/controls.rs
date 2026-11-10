//! Client control-page navigation shared by keyboard and controller input.
#[derive(Clone, Copy, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
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
    pub const ALL: [Self; 6] = [
        Self::Resume,
        Self::GamepadMouse,
        Self::Disconnect,
        Self::End,
        Self::Stats,
        Self::Fullscreen,
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
#[derive(Clone)]
pub struct Controls {
    pub order: [ControlAction; 6],
    pub editing: bool,
    pub moving: Option<usize>,
    pub selected: usize,
    pub confirm_end: bool,
    pub gamepad_mouse: bool,
    pub has_gamepad: bool,
    pub active: bool,
    pub bitrate: u32,
    pub bitrate_feedback: BitrateFeedback,
}

impl Default for Controls {
    fn default() -> Self { Self { order: ControlAction::ALL, editing: false, moving: None,
        selected: 0, confirm_end: false, gamepad_mouse: false, has_gamepad: false,
        active: false, bitrate: 0, bitrate_feedback: BitrateFeedback::default() } }
}
pub fn valid_order(order: &[ControlAction; 6]) -> bool {
    ControlAction::ALL.iter().all(|item| order.iter().filter(|a| *a == item).count() == 1)
}
pub fn position(index: usize) -> (f32, f32) {
    let angle = index as f32 * std::f32::consts::TAU / ControlAction::ALL.len() as f32 - std::f32::consts::FRAC_PI_2;
    (angle.cos() * 220., angle.sin() * 220.)
}
// Runtime requests coalesce in the worker. An older result can update the last
// applied value, but cannot clear or fail a newer pending request.
#[derive(Clone, Default)]
pub struct BitrateFeedback {
    sequence: u32,
    pending_id: Option<u32>,
    pub pending_kbps: Option<u32>,
    pub applied_kbps: Option<u32>,
    pub error: Option<String>,
}
impl BitrateFeedback {
    pub fn next_id(&self) -> u32 {
        if self.sequence == i32::MAX as u32 {
            1
        } else {
            self.sequence + 1
        }
    }
    pub fn request(&mut self, kbps: u32) -> u32 {
        self.sequence = self.next_id();
        self.pending_id = Some(self.sequence);
        self.pending_kbps = Some(kbps);
        self.error = None;
        self.sequence
    }
    pub fn result(&mut self, request_id: u32, kbps: u32, success: bool, message: String) {
        if !(1000..=150_000).contains(&kbps) {
            return;
        }
        if success {
            self.applied_kbps = Some(kbps);
        }
        if self.pending_id == Some(request_id) {
            self.pending_id = None;
            self.pending_kbps = None;
            self.error = (!success).then_some(message);
        }
    }
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
    pub fn navigate(&mut self, direction: &str) {
        let (dx, dy) = match direction { "up" => (0., -1.), "down" => (0., 1.),
            "left" => (-1., 0.), "right" => (1., 0.), _ => return };
        let (x, y) = position(self.selected);
        self.selected = (0..ControlAction::ALL.len()).filter(|&i| i != self.selected).filter_map(|i| {
            let (nx, ny) = position(i);
            let (vx, vy) = (nx - x, ny - y);
            let forward = vx * dx + vy * dy;
            (forward > 1.).then_some((i, (vx * vx + vy * vy) / forward))
        }).min_by(|a, b| a.1.total_cmp(&b.1)).map(|v| v.0).unwrap_or(self.selected);
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
            self.selected = self.order.iter().position(|a| *a == ControlAction::End).unwrap();
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
    fn reordered_actions_preserve_confirmation_and_spatial_navigation() {
        let mut controls = Controls { active: true, ..Default::default() };
        controls.order.swap(0, 3);
        assert!(valid_order(&controls.order));
        assert!(!controls.choose(ControlAction::End));
        assert_eq!(controls.selected, 0);
        assert!(controls.choose(ControlAction::End));
        for direction in ["up", "down", "left", "right"] {
            for index in 0..ControlAction::ALL.len() {
                controls.selected = index;
                controls.navigate(direction);
                assert!(controls.selected < ControlAction::ALL.len());
                let (x, y) = position(index);
                let (nx, ny) = position(controls.selected);
                if index != controls.selected {
                    assert!(match direction { "up" => ny < y, "down" => ny > y,
                        "left" => nx < x, _ => nx > x });
                }
            }
        }
        controls.order[0] = controls.order[1];
        assert!(!valid_order(&controls.order));
    }
    #[test]
    fn bitrate_results_preserve_newer_requests_and_failed_changes() {
        let mut feedback = BitrateFeedback::default();
        feedback.request(20_000);
        feedback.request(25_000);
        feedback.result(1, 20_000, true, String::new());
        assert_eq!(feedback.applied_kbps, Some(20_000));
        assert_eq!(feedback.pending_kbps, Some(25_000));
        feedback.result(2, 25_000, false, "Host rejected change".into());
        assert_eq!(feedback.applied_kbps, Some(20_000));
        assert_eq!(feedback.pending_kbps, None);
        assert_eq!(feedback.error.as_deref(), Some("Host rejected change"));
        feedback.request(30_000);
        feedback.result(2, 25_000, false, "Old failure".into());
        assert!(feedback.error.is_none());
        assert_eq!(feedback.pending_kbps, Some(30_000));
        feedback.result(3, 30_000, true, String::new());
        assert_eq!(feedback.applied_kbps, Some(30_000));
        assert_eq!(feedback.pending_kbps, None);
        feedback.result(3, 0, true, String::new());
        assert_eq!(feedback.applied_kbps, Some(30_000));
        feedback.request(30_000);
        feedback.result(3, 30_000, false, "Previous identical target".into());
        assert_eq!(feedback.pending_kbps, Some(30_000));
        assert!(feedback.error.is_none());
    }
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
