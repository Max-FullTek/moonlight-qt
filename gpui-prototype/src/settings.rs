use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub resolution: usize,
    pub fps: usize,
    pub codec: usize,
    pub bitrate: u32,
    pub abr: bool,
    pub fec: usize,
    pub fec_percent: u32,
    pub enhancement: bool,
    pub scale: bool,
    pub scale_percent: u32,
    pub window_mode: usize,
    pub decoder: usize,
    pub hdr: bool,
    pub vsync: bool,
    pub frame_pacing: bool,
    pub language: usize,
    pub advanced: std::collections::BTreeMap<String, serde_json::Value>,
}
pub const RESOLUTIONS: &[&str] = &[
    "1280 × 720",
    "1920 × 1080",
    "2560 × 1440",
    "3840 × 2160",
    "自訂解析度",
    "跟隨客戶端螢幕",
];
pub const FPS: &[&str] = &[
    "30 FPS",
    "60 FPS",
    "90 FPS",
    "120 FPS",
    "144 FPS",
    "自訂 FPS",
];
pub const CODECS: &[&str] = &["自動", "H.264", "HEVC（H.265）", "AV1"];
pub const WINDOW_MODES: &[&str] = &["全螢幕", "無邊框全螢幕", "視窗"];
pub const DECODERS: &[&str] = &["自動", "僅硬體解碼", "僅軟體解碼"];
pub const LANGUAGES: &[&str] = &["繁體中文", "简体中文", "English"];
pub const FEC: &[&str] = &["跟隨主機", "自動", "固定百分比"];
impl Default for Settings {
    fn default() -> Self {
        Self {
            resolution: 1,
            fps: 1,
            codec: 0,
            bitrate: 20,
            abr: false,
            fec: 0,
            fec_percent: 20,
            enhancement: false,
            scale: false,
            scale_percent: 100,
            window_mode: 2,
            decoder: 0,
            hdr: false,
            vsync: true,
            frame_pacing: false,
            language: 0,
            advanced: Default::default(),
        }
    }
}
impl Settings {
    pub fn option(&self, key: &str) -> serde_json::Value {
        self.advanced.get(key).cloned().unwrap_or_else(|| {
            crate::advanced::spec(key)
                .expect("known setting")
                .default_value()
        })
    }
    pub fn advanced_values(&self) -> serde_json::Value {
        serde_json::Value::Object(
            crate::advanced::OPTIONS
                .iter()
                .map(|s| (s.key.to_owned(), self.option(s.key)))
                .collect(),
        )
    }
    pub fn dimensions(&self) -> (u32, u32) {
        if self.resolution == 4 {
            (
                self.option("custom_width").as_f64().unwrap() as u32,
                self.option("custom_height").as_f64().unwrap() as u32,
            )
        } else {
            [
                (1280, 720),
                (1920, 1080),
                (2560, 1440),
                (3840, 2160),
                (1920, 1080),
                (1920, 1080),
            ][self.resolution]
        }
    }
    pub fn frame_rate(&self) -> u32 {
        if self.fps == 5 {
            self.option("custom_fps").as_f64().unwrap() as u32
        } else {
            [30, 60, 90, 120, 144][self.fps]
        }
    }

    pub fn validate(&self) -> bool {
        self.advanced
            .iter()
            .all(|(k, v)| crate::advanced::spec(k).is_some_and(|s| s.accepts(v)))
            && self.option("hdr_min").as_f64() <= self.option("hdr_average").as_f64()
            && self.option("hdr_average").as_f64() <= self.option("hdr_peak").as_f64()
            && self.resolution < RESOLUTIONS.len()
            && self.fps < FPS.len()
            && self.codec < CODECS.len()
            && self.fec < FEC.len()
            && self.window_mode < WINDOW_MODES.len()
            && self.decoder < DECODERS.len()
            && self.language < LANGUAGES.len()
            && (1..=150).contains(&self.bitrate)
            && self.fec_percent <= 100
            && (20..=100).contains(&self.scale_percent)
    }
    pub fn recommended_bitrate(&self) -> u32 {
        let (w, h) = self.dimensions();
        let pixels = f64::from(w) * f64::from(h);
        let table = [
            (640. * 360., 1.),
            (854. * 480., 2.),
            (1280. * 720., 5.),
            (1920. * 1080., 10.),
            (2560. * 1440., 20.),
            (3840. * 2160., 40.),
        ];
        let base = if pixels <= table[0].0 {
            table[0].1
        } else {
            table
                .windows(2)
                .find(|pair| pixels <= pair[1].0)
                .map(|pair| {
                    pair[0].1
                        + (pixels - pair[0].0) / (pair[1].0 - pair[0].0) * (pair[1].1 - pair[0].1)
                })
                .unwrap_or(40.)
        };
        let fps = self.frame_rate() as f64;
        let factor = if fps <= 60.0 {
            fps / 30.0
        } else {
            (fps / 60.0).sqrt() * 2.0
        };
        let chroma = if self.option("yuv444") == true {
            2.
        } else {
            1.
        };
        (base * factor * chroma).round().clamp(1.0, 150.0) as u32
    }
}
pub fn preferences_path() -> PathBuf {
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("MoonlightVPlusGpuiPrototype")
        .join("settings.json")
}
pub fn load() -> (Settings, String) {
    let path = preferences_path();
    if !path.exists() {
        return (Settings::default(), String::new());
    }
    match std::fs::read(&path)
        .ok()
        .and_then(|bytes| serde_json::from_slice::<Settings>(&bytes).ok())
    {
        Some(settings) if settings.validate() => (settings, String::new()),
        _ => (
            Settings::default(),
            "原型設定無法讀取，已使用預設值；原檔保留。".into(),
        ),
    }
}
pub fn save(settings: &Settings) -> anyhow::Result<()> {
    anyhow::ensure!(settings.validate(), "設定超出有效範圍");
    let path = preferences_path();
    std::fs::create_dir_all(path.parent().unwrap())?;
    std::fs::write(path, serde_json::to_vec_pretty(settings)?)?;
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reject_invalid_preferences_and_preserve_round_trip() {
        let settings = Settings::default();
        assert!(settings.validate());
        let restored: Settings =
            serde_json::from_slice(&serde_json::to_vec(&settings).unwrap()).unwrap();
        assert_eq!(restored, settings);
        assert!(
            !Settings {
                resolution: 99,
                ..settings.clone()
            }
            .validate()
        );
        assert!(
            !Settings {
                bitrate: 0,
                ..settings.clone()
            }
            .validate()
        );
        assert!(
            !Settings {
                scale_percent: 19,
                ..settings.clone()
            }
            .validate()
        );
        assert!(
            !Settings {
                fec_percent: 101,
                ..settings
            }
            .validate()
        );
    }
    #[test]
    fn migrate_existing_settings_and_validate_display_choices() {
        let old: Settings = serde_json::from_str(r#"{"bitrate":42,"resolution":2}"#).unwrap();
        assert_eq!(old.bitrate, 42);
        assert_eq!(old.resolution, 2);
        assert_eq!(old.window_mode, 2);
        assert!(old.vsync);
        assert!(!old.hdr);
        assert!(old.validate());
        assert_eq!(old.language, 0);
        assert!(
            !Settings {
                language: 3,
                ..old.clone()
            }
            .validate()
        );
        assert!(
            !Settings {
                window_mode: 3,
                ..old.clone()
            }
            .validate()
        );
        assert!(!Settings { decoder: 3, ..old }.validate());
    }
    #[test]
    fn bitrate_matches_native_policy_for_available_presets() {
        assert_eq!(Settings::default().recommended_bitrate(), 20);
        assert_eq!(
            Settings {
                resolution: 3,
                ..Settings::default()
            }
            .recommended_bitrate(),
            80
        );
        assert_eq!(
            Settings {
                fps: 3,
                ..Settings::default()
            }
            .recommended_bitrate(),
            28
        );
    }
    #[test]
    fn custom_settings_validate_persist_and_use_native_bitrate_interpolation() {
        let mut settings = Settings {
            resolution: 4,
            fps: 5,
            ..Settings::default()
        };
        settings
            .advanced
            .insert("custom_width".into(), serde_json::json!(854));
        settings
            .advanced
            .insert("custom_height".into(), serde_json::json!(480));
        settings
            .advanced
            .insert("custom_fps".into(), serde_json::json!(60));
        assert!(settings.validate());
        assert_eq!(settings.dimensions(), (854, 480));
        assert_eq!(settings.recommended_bitrate(), 4);
        settings
            .advanced
            .insert("yuv444".into(), serde_json::json!(true));
        assert_eq!(settings.recommended_bitrate(), 8);
        let restored: Settings =
            serde_json::from_slice(&serde_json::to_vec(&settings).unwrap()).unwrap();
        assert_eq!(restored, settings);
        settings
            .advanced
            .insert("custom_fps".into(), serde_json::json!(512));
        assert!(settings.validate());
        settings
            .advanced
            .insert("custom_fps".into(), serde_json::json!(513));
        assert!(!settings.validate());
        settings.advanced.remove("custom_fps");
        settings
            .advanced
            .insert("hdr_average".into(), serde_json::json!(1001));
        assert!(!settings.validate());
        settings.advanced.remove("hdr_average");
        settings
            .advanced
            .insert("deadzone".into(), serde_json::json!(2.5));
        assert!(!settings.validate());
        settings.advanced.remove("deadzone");
        settings
            .advanced
            .insert("microphone".into(), serde_json::json!(true));
        assert!(!settings.validate());
    }
}

// Window placement is separate from streaming preferences, so moving a window
// never silently saves a settings draft. Restore only bounds intersecting a display.
pub fn launcher_bounds(settings: &Settings) -> Option<gpui::Bounds<gpui::Pixels>> {
    if settings.option("remember_window") != true {
        return None;
    }
    let p = preferences_path().with_file_name("window.json");
    let [x, y, w, h]: [f32; 4] = serde_json::from_slice(&std::fs::read(p).ok()?).ok()?;
    if ![x, y, w, h].iter().all(|v| v.is_finite())
        || !(600.0..=16384.0).contains(&w)
        || !(500.0..=16384.0).contains(&h)
    {
        return None;
    }
    Some(gpui::Bounds {
        origin: gpui::point(gpui::px(x), gpui::px(y)),
        size: gpui::size(gpui::px(w), gpui::px(h)),
    })
}
pub fn remember_bounds(settings: &Settings, window: &gpui::Window) {
    if settings.option("remember_window") != true {
        return;
    }
    let b = window.window_bounds().get_bounds();
    let data = [
        f32::from(b.origin.x),
        f32::from(b.origin.y),
        f32::from(b.size.width),
        f32::from(b.size.height),
    ];
    thread_local! {static LAST: std::cell::Cell<Option<[f32;4]>> = const {std::cell::Cell::new(None)};}
    LAST.with(|last| {
        if last.get() != Some(data) {
            last.set(Some(data));
            let p = preferences_path().with_file_name("window.json");
            if let Some(parent) = p.parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            if let Ok(bytes) = serde_json::to_vec(&data) {
                let _ = std::fs::write(p, bytes);
            }
        }
    });
}
