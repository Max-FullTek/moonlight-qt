//! Retained Windows settings: controls, defaults and validation share this schema.
use serde_json::{Value, json};
#[derive(Clone, Copy)]
pub enum Kind {
    Toggle(bool),
    Choice(&'static [&'static str], usize),
    Number(f64, f64, f64),
    Text,
}
pub struct OptionSpec {
    pub key: &'static str,
    pub category: &'static str,
    pub label: &'static str,
    pub hint: &'static str,
    pub kind: Kind,
}
use Kind::*;
pub const OPTIONS: &[OptionSpec] = &[
    OptionSpec {
        key: "host_audio",
        category: "audio",
        label: "主機同時播放音效",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "audio",
        category: "audio",
        label: "音效配置",
        hint: "",
        kind: Choice(&["Stereo", "5.1", "7.1", "7.1.4"], 0),
    },
    OptionSpec {
        key: "mute_background",
        category: "audio",
        label: "切換到其他視窗時靜音",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "screen_mode",
        category: "host",
        label: "主機螢幕組合模式",
        hint: "",
        kind: Choice(
            &[
                "跟隨主機",
                "不變更",
                "確保啟用",
                "設為主螢幕",
                "只啟用此螢幕",
                "設為副螢幕",
            ],
            0,
        ),
    },
    OptionSpec {
        key: "vdd",
        category: "host",
        label: "串流螢幕類型",
        hint: "",
        kind: Choice(&["跟隨主機", "實體螢幕", "虛擬螢幕"], 0),
    },
    OptionSpec {
        key: "optimize",
        category: "host",
        label: "最佳化主機遊戲設定",
        hint: "",
        kind: Toggle(true),
    },
    OptionSpec {
        key: "quit_host",
        category: "host",
        label: "結束串流時關閉主機程式",
        hint: "開啟後可能失去未儲存的進度。",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "desktop_mouse",
        category: "input",
        label: "遠端桌面滑鼠模式",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "local_cursor",
        category: "input",
        label: "顯示本機游標",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "capture",
        category: "input",
        label: "攔截系統快捷鍵",
        hint: "",
        kind: Choice(&["不攔截", "僅全螢幕", "一律攔截"], 1),
    },
    OptionSpec {
        key: "swap_mouse",
        category: "input",
        label: "交換滑鼠左右鍵",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "swap_alt",
        category: "input",
        label: "交換 Alt 與 Win 鍵",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "reverse_scroll",
        category: "input",
        label: "反轉捲動方向",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "touchpad",
        category: "input",
        label: "觸控螢幕使用虛擬觸控板",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "precision_touchpad",
        category: "input",
        label: "使用精確式觸控板輸入",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "swap_face",
        category: "gamepad",
        label: "交換 A／B 與 X／Y",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "deadzone",
        category: "gamepad",
        label: "搖桿死區（%）",
        hint: "",
        kind: Number(0.0, 0.0, 30.0),
    },
    OptionSpec {
        key: "quit_combo",
        category: "gamepad",
        label: "結束串流手把組合鍵",
        hint: "",
        kind: Choice(
            &[
                "Start + Select + LB + RB",
                "Select + LB + RB + Y",
                "Start + LB + RB + A",
                "Start + LB + RB + B",
                "LB + RB + X + Y",
                "LB + RB + A + B",
            ],
            0,
        ),
    },
    OptionSpec {
        key: "force_gamepad",
        category: "gamepad",
        label: "強制第一支手把保持連線",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "gamepad_mouse",
        category: "gamepad",
        label: "長按 Start 使用手把滑鼠",
        hint: "",
        kind: Toggle(true),
    },
    OptionSpec {
        key: "background_gamepad",
        category: "gamepad",
        label: "背景接收手把輸入",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "haptics",
        category: "gamepad",
        label: "DualSense 進階觸覺",
        hint: "",
        kind: Choice(&["實體 DualSense", "模擬 DualSense"], 0),
    },
    OptionSpec {
        key: "stats",
        category: "compatibility",
        label: "串流時顯示效能統計",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "overlay",
        category: "compatibility",
        label: "串流控制選單位置",
        hint: "",
        kind: Choice(&["停用", "浮動按鈕", "頂部邊緣", "右側邊緣", "左側邊緣"], 1),
    },
    OptionSpec {
        key: "yuv444",
        category: "compatibility",
        label: "啟用 YUV 4:4:4",
        hint: "",
        kind: Toggle(false),
    },
    OptionSpec {
        key: "connection_warnings",
        category: "interface",
        label: "顯示連線品質警告",
        hint: "",
        kind: Toggle(true),
    },
    OptionSpec {
        key: "configuration_warnings",
        category: "interface",
        label: "顯示設定警告",
        hint: "",
        kind: Toggle(true),
    },
    OptionSpec {
        key: "keep_awake",
        category: "interface",
        label: "串流時保持螢幕喚醒",
        hint: "",
        kind: Toggle(true),
    },
    OptionSpec {
        key: "ui_window",
        category: "interface",
        label: "啟動時的視窗模式",
        hint: "",
        kind: Choice(&["視窗", "最大化", "全螢幕"], 0),
    },
    OptionSpec {
        key: "remember_window",
        category: "interface",
        label: "記住視窗位置與大小",
        hint: "",
        kind: Toggle(true),
    },
    OptionSpec {
        key: "hdr_format",
        category: "display",
        label: "HDR 格式",
        hint: "",
        kind: Choice(&["HDR10 (PQ)", "HLG"], 0),
    },
    OptionSpec {
        key: "hdr_brightness",
        category: "display",
        label: "HDR 亮度設定",
        hint: "",
        kind: Choice(&["使用主機預設", "自動偵測客戶端", "手動設定"], 0),
    },
    OptionSpec {
        key: "hdr_peak",
        category: "display",
        label: "峰值亮度（nits）",
        hint: "",
        kind: Number(1000.0, 1.0, 10000.0),
    },
    OptionSpec {
        key: "hdr_min",
        category: "display",
        label: "最低亮度（nits）",
        hint: "",
        kind: Number(0.001, 0.0, 10.0),
    },
    OptionSpec {
        key: "hdr_average",
        category: "display",
        label: "全畫面亮度（nits）",
        hint: "",
        kind: Number(1000.0, 1.0, 10000.0),
    },
    OptionSpec {
        key: "client_display",
        category: "display",
        label: "客戶端螢幕編號（0 為跟隨視窗）",
        hint: "",
        kind: Number(0.0, 0.0, 16.0),
    },
    OptionSpec {
        key: "custom_width",
        category: "stream",
        label: "自訂寬度",
        hint: "",
        kind: Number(1920.0, 256.0, 8192.0),
    },
    OptionSpec {
        key: "custom_height",
        category: "stream",
        label: "自訂高度",
        hint: "",
        kind: Number(1080.0, 256.0, 8192.0),
    },
    OptionSpec {
        key: "custom_fps",
        category: "stream",
        label: "自訂 FPS",
        hint: "",
        kind: Number(60.0, 1.0, 512.0),
    },
    OptionSpec {
        key: "host_display",
        category: "host",
        label: "主機螢幕識別碼（留空跟隨主機）",
        hint: "由主機回報的清單選取，也可輸入 Sunshine 的螢幕識別碼。",
        kind: Text,
    },
];
pub fn spec(key: &str) -> Option<&'static OptionSpec> {
    OPTIONS.iter().find(|s| s.key == key)
}
impl OptionSpec {
    pub fn default_value(&self) -> Value {
        match self.kind {
            Toggle(b) => json!(b),
            Choice(_, v) => json!(v),
            Number(v, _, _) => json!(v),
            Text => json!(""),
        }
    }
    pub fn accepts(&self, value: &Value) -> bool {
        match self.kind {
            Toggle(_) => value.is_boolean(),
            Choice(items, _) => value.as_u64().is_some_and(|v| v < items.len() as u64),
            Number(_, low, high) => value.as_f64().is_some_and(|v| {
                v.is_finite()
                    && v >= low
                    && v <= high
                    && (self.key.starts_with("hdr_") || v.fract() == 0.)
            }),
            Text => value.as_str().is_some_and(|v| {
                v.encode_utf16().count() <= 256 && !v.chars().any(char::is_control)
            }),
        }
    }
}
