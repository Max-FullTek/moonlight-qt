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
}
pub const RESOLUTIONS: &[&str] = &["1280 × 720", "1920 × 1080", "2560 × 1440", "3840 × 2160"];
pub const FPS: &[&str] = &["30 FPS", "60 FPS", "90 FPS", "120 FPS", "144 FPS"];
pub const CODECS: &[&str] = &["自動", "H.264", "HEVC（H.265）", "AV1"];
pub const WINDOW_MODES: &[&str] = &["全螢幕", "無邊框全螢幕", "視窗"];
pub const DECODERS: &[&str] = &["自動", "僅硬體解碼", "僅軟體解碼"];
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
        }
    }
}
impl Settings {
    pub fn validate(&self) -> bool {
        self.resolution < RESOLUTIONS.len()
            && self.fps < FPS.len()
            && self.codec < CODECS.len()
            && self.fec < FEC.len()
            && self.window_mode < WINDOW_MODES.len()
            && self.decoder < DECODERS.len()
            && (1..=150).contains(&self.bitrate)
            && self.fec_percent <= 100
            && (20..=100).contains(&self.scale_percent)
    }
    pub fn recommended_bitrate(&self) -> u32 {
        let base = [5.0_f64, 10.0, 20.0, 40.0][self.resolution];
        let fps = [30.0_f64, 60.0, 90.0, 120.0, 144.0][self.fps];
        let factor = if fps <= 60.0 {
            fps / 30.0
        } else {
            (fps / 60.0).sqrt() * 2.0
        };
        (base * factor).round().clamp(1.0, 150.0) as u32
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
}
