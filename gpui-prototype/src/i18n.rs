//! UI catalog for the Windows PC client. Host/app names bypass this catalog.
pub(crate) const CATALOG: &[(&str, &str, &str)] = &[
    ("裝置", "设备", "Devices"),
    ("設定", "设置", "Settings"),
    ("結束連線", "结束连接", "Disconnect"),
    ("關於", "关于", "About"),
    ("主機目前離線", "主机目前离线", "Host is offline"),
    (
        "請確認主機與 Tailscale 網路，再重新選取裝置。",
        "请检查主机与 Tailscale 网络，再重新选择设备。",
        "Check the host and Tailscale connection, then select the device again.",
    ),
    ("正在配對", "正在配对", "Pairing"),
    (
        "正在向主機要求配對 PIN…",
        "正在向主机请求配对 PIN…",
        "Requesting a pairing PIN from the host…",
    ),
    (
        "關於 GPUI 整合版",
        "关于 GPUI 集成版",
        "About the GPUI client",
    ),
    (
        "Rust + GPUI 原生啟動器；C++ 核心暫時保留 Qt 後端及 SDL/D3D11 串流。核心模式不載入 QML。此階段先整合手動主機、配對、程式清單與串流／顯示設定；其餘設定尚未移植。",
        "Rust + GPUI 原生启动器；C++ 核心暂时保留 Qt 后端及 SDL/D3D11 串流。核心模式不加载 QML。目前已接入手动主机、配对、程序列表及串流／显示设置；其余设置尚未迁移。",
        "Native Rust + GPUI launcher for Windows PC. The C++ worker retains Qt backend services and SDL/D3D11 streaming without loading QML. Additional settings are still being integrated.",
    ),
    (
        "核心尚未就緒，或目前仍有操作進行中。",
        "核心尚未就绪，或仍有操作正在进行。",
        "The core is not ready, or another operation is active.",
    ),
    (
        "請輸入 IP 或主機名稱，可附連接埠；不要輸入網址。",
        "请输入 IP 或主机名，可附端口；不要输入网址。",
        "Enter an IP address or hostname, optionally with a port. Do not enter a URL.",
    ),
    ("正在連線", "正在连接", "Connecting"),
    (
        "正在取得主機資訊…",
        "正在获取主机信息…",
        "Retrieving host information…",
    ),
    (
        "操作仍在進行；點選裝置前請等待完成。",
        "操作仍在进行；请选择设备前等待完成。",
        "An operation is still active. Wait before selecting a device.",
    ),
    ("正在啟動串流…", "正在启动串流…", "Starting the stream…"),
    (
        "已載入預設值，按儲存後保留。",
        "已加载默认值，保存后生效。",
        "Defaults loaded. Save to keep them.",
    ),
    (
        "已儲存；下次串流使用此設定。",
        "已保存；下次串流使用此设置。",
        "Saved. These settings apply to the next stream.",
    ),
    ("核心未啟動", "核心未启动", "Core is not running"),
    ("核心錯誤", "核心错误", "Core error"),
    (
        "核心已就緒，請新增或選取裝置。",
        "核心已就绪，请添加或选择设备。",
        "Core ready. Add or select a device.",
    ),
    (
        "主機已新增，請點選卡片配對。",
        "主机已添加，请选择卡片进行配对。",
        "Host added. Select its card to pair.",
    ),
    ("配對成功。", "配对成功。", "Pairing successful."),
    (
        "串流進行中；關閉 SDL 視窗或按下方按鈕結束連線。",
        "串流进行中；关闭 SDL 窗口或点击下方按钮结束连接。",
        "Streaming. Close the SDL window or use Disconnect below to end the session.",
    ),
    (
        "串流已結束；主機上的程式保持開啟。",
        "串流已结束；主机上的程序保持运行。",
        "Disconnected. The application remains running on the host.",
    ),
    ("操作失敗", "操作失败", "Operation failed"),
    (
        "GPUI 核心整合測試 · 手動連線 · SDL 串流",
        "GPUI 核心集成测试 · 手动连接 · SDL 串流",
        "GPUI integration · Manual connection · SDL streaming",
    ),
    (
        "區網自動搜尋已關閉。使用手動 IP／Tailscale；串流使用主螢幕。",
        "局域网自动发现已关闭。使用手动 IP／Tailscale；串流使用主显示器。",
        "LAN discovery is off. Use a manual IP or Tailscale. Streaming uses the primary display.",
    ),
    ("+ 新增裝置", "+ 添加设备", "+ Add device"),
    (
        "尚未新增裝置。按「新增裝置」輸入主機 IP。",
        "尚未添加设备。点击“添加设备”输入主机 IP。",
        "No devices yet. Select Add device to enter a host IP.",
    ),
    ("離線", "离线", "Offline"),
    ("已配對 · 在線", "已配对 · 在线", "Paired · Online"),
    ("尚未配對 · 在線", "未配对 · 在线", "Not paired · Online"),
    ("‹ 返回裝置", "‹ 返回设备", "‹ Back to devices"),
    (
        "正在取得程式清單；請確認 Sunshine 已設定桌面或遊戲。",
        "正在获取程序列表；请确认 Sunshine 已配置桌面或游戏。",
        "Retrieving applications. Check that Sunshine has a desktop or game configured.",
    ),
    ("開始串流", "开始串流", "Start stream"),
    ("使用建議值", "使用建议值", "Recommended"),
    ("串流", "串流", "Stream"),
    ("顯示", "显示", "Display"),
    ("音效", "音频", "Audio"),
    ("主機", "主机", "Host"),
    ("鍵盤與滑鼠", "键盘与鼠标", "Keyboard and mouse"),
    ("手把", "手柄", "Gamepad"),
    (
        "相容性與診斷",
        "兼容性与诊断",
        "Compatibility and diagnostics",
    ),
    ("介面", "界面", "Interface"),
    (
        "已開放串流、顯示與介面語言，其餘分類待後續移植。",
        "已开放串流、显示与界面语言，其余类别尚待迁移。",
        "Stream, display and interface language are available. Other categories are pending.",
    ),
    (
        "GPUI 整合版 · 儲存後套用至下次串流",
        "GPUI 集成版 · 保存后应用于下次串流",
        "GPUI client · Save changes for the next stream",
    ),
    ("畫面", "画面", "Video"),
    ("解析度", "分辨率", "Resolution"),
    (
        "初版提供常用預設；自訂輸入待補。",
        "目前提供常用预设；自定义输入尚待添加。",
        "Common presets are available. Custom values are pending.",
    ),
    ("影格率", "帧率", "Frame rate"),
    ("視訊編碼", "视频编码", "Video codec"),
    ("位元率上限", "码率上限", "Bitrate limit"),
    ("位元率", "码率", "Bitrate"),
    ("連線穩定性", "连接稳定性", "Connection stability"),
    (
        "自動調整位元率（ABR）",
        "自动调整码率（ABR）",
        "Adaptive bitrate (ABR)",
    ),
    (
        "需要相容的 Sunshine 主機。",
        "需要兼容的 Sunshine 主机。",
        "Requires a compatible Sunshine host.",
    ),
    (
        "封包遺失保護（FEC）",
        "丢包保护（FEC）",
        "Forward error correction (FEC)",
    ),
    (
        "與 ABR 可獨立使用；取決於主機支援。",
        "可独立于 ABR 使用；取决于主机支持。",
        "Independent of ABR. Host support is required.",
    ),
    ("固定 FEC 百分比", "固定 FEC 百分比", "Fixed FEC percentage"),
    ("畫質增強", "画质增强", "Video enhancement"),
    ("AI 畫質增強", "AI 画质增强", "AI video enhancement"),
    (
        "由現有核心選擇解碼器；尚未在此頁偵測 GPU 支援。",
        "由现有核心选择解码器；此页尚未检测 GPU 支持。",
        "The core selects the decoder. GPU capabilities are not detected on this page yet.",
    ),
    ("串流渲染比例", "串流渲染比例", "Render scaling"),
    ("渲染比例", "渲染比例", "Render scale"),
    ("有尚未儲存的變更", "有尚未保存的更改", "Unsaved changes"),
    (
        "下次串流使用已儲存的設定",
        "下次串流使用已保存的设置",
        "The next stream uses saved settings",
    ),
    ("載入預設值", "加载默认值", "Load defaults"),
    ("儲存設定", "保存设置", "Save settings"),
    ("顯示模式", "显示模式", "Display mode"),
    ("啟動顯示模式", "启动显示模式", "Startup display mode"),
    (
        "串流中按 Ctrl + Alt + Shift + X 切換全螢幕／視窗。",
        "串流时按 Ctrl + Alt + Shift + X 切换全屏／窗口。",
        "Press Ctrl + Alt + Shift + X while streaming to toggle fullscreen.",
    ),
    (
        "需要相容的主機、編碼器與 HDR 螢幕；由現有核心檢查支援。",
        "需要兼容的主机、编码器与 HDR 显示器；由现有核心检查支持。",
        "Requires a compatible host, encoder and HDR display. The core checks compatibility.",
    ),
    (
        "垂直同步（V-Sync）",
        "垂直同步（V-Sync）",
        "Vertical sync (V-Sync)",
    ),
    (
        "減少畫面撕裂，可能增加延遲。",
        "减少画面撕裂，可能增加延迟。",
        "Reduces tearing and may add latency.",
    ),
    ("影格平順處理", "帧平滑处理", "Frame pacing"),
    (
        "只在垂直同步開啟時生效。",
        "仅在垂直同步开启时生效。",
        "Only effective when V-Sync is enabled.",
    ),
    ("視訊解碼器", "视频解码器", "Video decoder"),
    (
        "優先使用自動；強制硬體模式不支援時由核心回報錯誤。",
        "建议使用自动；强制硬件模式不受支持时由核心报告错误。",
        "Auto is recommended. The core reports unsupported forced hardware decoding.",
    ),
    (
        "目前串流在主螢幕啟動；螢幕選擇與其他進階顯示設定尚未移植。",
        "目前在主显示器启动串流；显示器选择及其他高级显示设置尚未迁移。",
        "Streams currently open on the primary display. Monitor selection and advanced display settings are pending.",
    ),
    ("新增裝置", "添加设备", "Add device"),
    (
        "輸入 IP／主機名稱，可附連接埠。支援貼上；Ctrl+A 清空。",
        "输入 IP／主机名，可附端口。支持粘贴；Ctrl+A 清空。",
        "Enter an IP or hostname, optionally with a port. Paste is supported. Ctrl+A clears the field.",
    ),
    ("連線", "连接", "Connect"),
    ("配對主機", "配对主机", "Pair host"),
    (
        "請在 Sunshine 的 PIN 配對頁輸入下列驗證碼，並等待配對完成。",
        "请在 Sunshine 的 PIN 配对页面输入以下验证码，等待配对完成。",
        "Enter this PIN on the Sunshine pairing page and wait for pairing to finish.",
    ),
    ("串流連線", "串流连接", "Streaming connection"),
    ("關閉", "关闭", "Close"),
    (
        "請使用整合封裝：同一資料夾內需要 Moonlight.exe 核心。",
        "请使用集成包：同一文件夹中需要 Moonlight.exe 核心。",
        "Use the integrated package. Moonlight.exe must be beside this frontend.",
    ),
    (
        "核心通訊中斷。",
        "核心通信中断。",
        "Communication with the core was interrupted.",
    ),
    (
        "核心版本或訊息格式不相容。",
        "核心版本或消息格式不兼容。",
        "The core version or message format is incompatible.",
    ),
    (
        "核心程序已結束；請重新開啟 GPUI。",
        "核心进程已结束；请重新启动 GPUI。",
        "The core process exited. Restart the GPUI client.",
    ),
    ("自動", "自动", "Auto"),
    ("全螢幕", "全屏", "Fullscreen"),
    ("無邊框全螢幕", "无边框全屏", "Borderless fullscreen"),
    ("視窗", "窗口", "Windowed"),
    ("僅硬體解碼", "仅硬件解码", "Hardware decoding only"),
    ("僅軟體解碼", "仅软件解码", "Software decoding only"),
    ("跟隨主機", "跟随主机", "Follow host"),
    ("固定百分比", "固定百分比", "Fixed percentage"),
    (
        "原型設定無法讀取，已使用預設值；原檔保留。",
        "无法读取设置，已使用默认值；原文件保留。",
        "Settings could not be read. Defaults are in use; the original file is preserved.",
    ),
    (
        "設定超出有效範圍",
        "设置超出有效范围",
        "Settings are out of range",
    ),
    ("開啟程式清單", "打开程序列表", "Open app list"),
    ("隱藏已隱藏項目", "隐藏已隐藏项目", "Hide hidden apps"),
    ("顯示隱藏項目", "显示隐藏项目", "Show hidden apps"),
    (
        "選取主機時自動啟動",
        "选择主机时自动启动",
        "Launch when selecting this host",
    ),
    ("程式選項", "程序选项", "App options"),
    (
        "隱藏只影響此客戶端的清單；直啟會在選取主機時啟動此程式。",
        "隐藏仅影响此客户端的列表；直启会在选择主机时启动此程序。",
        "Hiding only affects this client's app list. Direct launch starts this app when selecting the host.",
    ),
    ("取消隱藏", "取消隐藏", "Unhide"),
    ("隱藏此程式", "隐藏此程序", "Hide this app"),
    ("取消直啟", "取消直启", "Disable direct launch"),
    ("設為直啟", "设为直启", "Set as direct launch"),
    ("介面語言", "界面语言", "Interface language"),
    (
        "保留繁體中文、简体中文與 English；不影響主機名稱或剪貼簿內容。",
        "提供繁體中文、简体中文及 English；不影响主机名称或剪贴板内容。",
        "Choose Traditional Chinese, Simplified Chinese or English. Host names and clipboard content are unchanged.",
    ),
    ("無法啟動核心", "无法启动核心", "Cannot start core"),
    ("無法儲存", "无法保存", "Cannot save"),
    ("主機未找到", "未找到主机", "Host not found."),
    ("程式未找到", "未找到程序", "Application not found."),
    (
        "程式偏好設定無效",
        "程序偏好设置无效",
        "Application preferences are invalid.",
    ),
    (
        "無法隱藏正在執行的程式",
        "无法隐藏正在运行的程序",
        "A running application cannot be hidden.",
    ),
    (
        "主機必須在線且已配對",
        "主机必须在线且已配对",
        "Host must be online and paired.",
    ),
    (
        "主機已有其他程式執行；請先在主機結束它。",
        "主机有其他程序正在运行；请先在主机结束该程序。",
        "Another app is running on the host. End it there before switching.",
    ),
];

pub fn text(language: usize, value: &str) -> String {
    if let Some(&(traditional, simplified, english)) = CATALOG
        .iter()
        .find(|(tw, cn, en)| value == *tw || value == *cn || value == *en)
    {
        return match language {
            1 => simplified,
            2 => english,
            _ => traditional,
        }
        .to_owned();
    }
    // Preserve technical diagnostics while translating known error prefixes.
    if let Some((prefix, detail)) = value.split_once('：') {
        let translated = text(language, prefix);
        if translated != prefix {
            return format!("{}: {}", translated, text(language, detail));
        }
    }
    value.to_owned()
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exactly_three_catalogs_preserve_user_text_and_translate_both_directions() {
        assert_eq!(text(0, "Devices"), "裝置");
        assert_eq!(text(1, "裝置"), "设备");
        assert_eq!(text(2, "裝置"), "Devices");
        assert_eq!(text(1, "我的電腦 😀"), "我的電腦 😀");
        let mut keys = std::collections::HashSet::new();
        for (tw, cn, en) in CATALOG {
            assert!(keys.insert(tw));
            assert!(!cn.is_empty() && !en.is_empty());
        }
    }
}
