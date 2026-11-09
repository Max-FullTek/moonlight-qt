//! UI catalog for the Windows PC client. Host/app names bypass this catalog.
pub(crate) const CATALOG: &[(&str, &str, &str)] = &[
    ("正在套用位元率：", "正在应用码率：", "Applying bitrate:"),
    (
        "位元率請求完成：",
        "码率请求完成：",
        "Bitrate request completed:",
    ),
    (
        "位元率調整失敗：",
        "码率调整失败：",
        "Bitrate change failed:",
    ),
    ("串流控制", "串流控制", "Stream controls"),
    ("返回串流", "返回串流", "Return to stream"),
    ("暫時斷線", "暂时断开", "Disconnect and keep session"),
    ("結束工作階段", "结束会话", "End host session"),
    (
        "再次確認：結束工作階段",
        "再次确认：结束会话",
        "Confirm: end host session",
    ),
    (
        "手把滑鼠：未連接手把",
        "手柄鼠标：未连接手柄",
        "Gamepad mouse: no controller connected",
    ),
    (
        "手把滑鼠：開啟（操作控制頁期間暫停）",
        "手柄鼠标：开启（操作控制页期间暂停）",
        "Gamepad mouse: on (paused while using controls)",
    ),
    ("手把滑鼠：關閉", "手柄鼠标：关闭", "Gamepad mouse: off"),
    (
        "方向鍵／手把十字鍵選擇，Enter／A 確認，Esc／B 返回串流。",
        "方向键／手柄十字键选择，Enter／A 确认，Esc／B 返回串流。",
        "Arrows / D-pad to navigate, Enter / A to select, Esc / B to return.",
    ),
    (
        "暫時斷線會保留 Desktop 工作階段；結束工作階段會請主機清理並依設定還原顯示。",
        "暂时断开会保留 Desktop 会话；结束会话会请主机清理并按设置还原显示。",
        "Disconnect keeps the Desktop session. End session asks the host to clean up and restore displays according to its settings.",
    ),
    (
        "串流進行中；Ctrl+Alt+Shift+O 開啟控制頁。",
        "串流进行中；Ctrl+Alt+Shift+O 打开控制页。",
        "Streaming; press Ctrl+Alt+Shift+O to open controls.",
    ),
    (
        "效能統計開關",
        "性能统计开关",
        "Toggle performance statistics",
    ),
    ("全螢幕開關", "全屏开关", "Toggle fullscreen"),
    ("滑鼠模式切換", "鼠标模式切换", "Switch mouse mode"),
    ("游標顯示開關", "光标显示开关", "Toggle cursor visibility"),
    (
        "游標範圍鎖定開關",
        "光标范围锁定开关",
        "Toggle pointer confinement",
    ),
    ("降低位元率", "降低码率", "Decrease bitrate"),
    ("提高位元率", "提高码率", "Increase bitrate"),
    ("正在結束連線…", "正在结束连接…", "Disconnecting…"),
    (
        "控制操作無效。",
        "控制操作无效。",
        "Invalid control action.",
    ),
    (
        "客戶端程序無效。",
        "客户端进程无效。",
        "Invalid client process.",
    ),
    (
        "控制頁無法使用，或無法恢復串流焦點。",
        "控制页不可用，或无法恢复串流焦点。",
        "Controls are unavailable or stream focus could not be restored.",
    ),
    ("HDR 格式", "HDR 格式", "HDR format"),
    (
        "所選客戶端螢幕已無法使用。",
        "所选客户端屏幕已不可用。",
        "Selected client monitor is no longer available.",
    ),
    (
        "無法讀取所選客戶端螢幕解析度。",
        "无法读取所选客户端屏幕分辨率。",
        "Unable to read the selected client monitor resolution.",
    ),
    ("裝置選項", "设备选项", "Device options"),
    ("重新命名", "重命名", "Rename"),
    ("移除裝置", "移除设备", "Remove device"),
    ("確認移除", "确认移除", "Confirm removal"),
    (
        "僅移除此客戶端的主機記錄；之後可重新新增並配對。",
        "仅移除此客户端的主机记录；之后可重新添加并配对。",
        "Remove this client's host record. You can add and pair it again later.",
    ),
    (
        "主機名稱須為 1～128 個字元。",
        "主机名称须为 1～128 个字符。",
        "Enter a host name between 1 and 128 characters.",
    ),
    ("主機名稱已更新。", "主机名称已更新。", "Host name updated."),
    (
        "已移除此客戶端的主機記錄。",
        "已移除此客户端的主机记录。",
        "Host removed from this client.",
    ),
    ("結束主機程式", "结束主机程序", "Quit host application"),
    ("確認結束程式", "确认结束程序", "Confirm quit"),
    (
        "未儲存的進度可能遺失。確定要結束主機上正在執行的程式？",
        "未保存的进度可能丢失。确定要结束主机上正在运行的程序？",
        "Unsaved progress may be lost. Quit the application running on the host?",
    ),
    ("正在結束程式", "正在结束程序", "Quitting application"),
    (
        "等待主機結束程式…",
        "等待主机结束程序…",
        "Waiting for the host to quit the application…",
    ),
    (
        "主機程式已結束。",
        "主机程序已结束。",
        "Host application has quit.",
    ),
    ("恢復串流", "恢复串流", "Resume stream"),
    ("串流已結束。", "串流已结束。", "Stream ended."),
    ("套用", "应用", "Apply"),
    ("新增主機", "添加主机", "Add host"),
    ("開始 PIN 碼配對", "开始 PIN 码配对", "Start PIN pairing"),
    (
        "主機已新增，請按「開始 PIN 碼配對」。",
        "主机已添加，请按“开始 PIN 码配对”。",
        "Host added. Select Start PIN pairing.",
    ),
    (
        "主機未回報螢幕，可跟隨主機設定或手動輸入識別碼。",
        "主机未报告屏幕，可跟随主机设置或手动输入标识码。",
        "No displays reported. Follow host settings or enter a display ID.",
    ),
    (
        "已讀取主機螢幕清單。",
        "已读取主机屏幕列表。",
        "Host display list loaded.",
    ),
    (
        "正在讀取主機螢幕清單…",
        "正在读取主机屏幕列表…",
        "Loading host displays…",
    ),
    (
        "請先在裝置頁選取已配對的主機。",
        "请先在设备页选择已配对的主机。",
        "First select a paired host on the Devices page.",
    ),
    (
        "讀取已選主機的螢幕清單",
        "读取所选主机的屏幕列表",
        "Load selected host's displays",
    ),
    (
        "輸入值超出有效範圍。",
        "输入值超出有效范围。",
        "Value is outside the valid range.",
    ),
    (
        "輸入新值；支援貼上，Ctrl+A 清空。",
        "输入新值；支持粘贴，Ctrl+A 清空。",
        "Enter a value; paste supported. Ctrl+A clears the field.",
    ),
    (
        "可使用預設、自訂解析度或跟隨客戶端所在螢幕。",
        "可使用预设、自定义分辨率或跟随客户端所在屏幕。",
        "Use a preset, custom resolution, or the client's monitor resolution.",
    ),
    (
        "使用手動 IP／Tailscale 連線；串流依已儲存的顯示設定啟動。",
        "使用手动 IP／Tailscale 连接；串流按已保存的显示设置启动。",
        "Connect by IP or Tailscale; streams use saved display settings.",
    ),
    (
        "Windows PC 客戶端：GPUI 介面搭配 C++／SDL 串流核心。僅手動連線；不提供 USB/IP、檔案傳送或自動下載更新。",
        "Windows PC 客户端：GPUI 界面搭配 C++／SDL 串流核心。仅手动连接；不提供 USB/IP、文件传输或自动下载更新。",
        "Windows PC client with GPUI and the C++/SDL streaming core. Manual connections; USB/IP, file transfer and automatic updates are excluded.",
    ),
    ("不變更", "不更改", "Leave unchanged"),
    ("確保啟用", "确保启用", "Ensure active"),
    ("設為主螢幕", "设为主屏幕", "Make primary"),
    ("只啟用此螢幕", "仅启用此屏幕", "Use this display only"),
    ("設為副螢幕", "设为副屏幕", "Make secondary"),
    ("實體螢幕", "实体屏幕", "Physical display"),
    ("虛擬螢幕", "虚拟屏幕", "Virtual display"),
    ("不攔截", "不拦截", "Never capture"),
    ("僅全螢幕", "仅全屏", "Fullscreen only"),
    ("一律攔截", "始终拦截", "Always capture"),
    ("實體 DualSense", "实体 DualSense", "Physical DualSense"),
    ("模擬 DualSense", "模拟 DualSense", "Emulated DualSense"),
    ("停用", "禁用", "Disabled"),
    ("浮動按鈕", "浮动按钮", "Floating button"),
    ("頂部邊緣", "顶部边缘", "Top edge"),
    ("右側邊緣", "右侧边缘", "Right edge"),
    ("左側邊緣", "左侧边缘", "Left edge"),
    ("最大化", "最大化", "Maximized"),
    ("使用主機預設", "使用主机默认值", "Use host defaults"),
    (
        "自動偵測客戶端",
        "自动检测客户端",
        "Detect client brightness",
    ),
    ("手動設定", "手动设置", "Manual"),
    (
        "開啟後可能失去未儲存的進度。",
        "开启后可能丢失未保存的进度。",
        "Unsaved host application progress may be lost.",
    ),
    (
        "由主機回報的清單選取，也可輸入 Sunshine 的螢幕識別碼。",
        "从主机报告的列表选择，也可输入 Sunshine 的屏幕标识码。",
        "Select a reported display or enter a Sunshine display ID.",
    ),
    ("主機同時播放音效", "主机同时播放音效", "Play audio on host"),
    ("音效配置", "音效配置", "Audio configuration"),
    (
        "切換到其他視窗時靜音",
        "切换到其他窗口时静音",
        "Mute stream when unfocused",
    ),
    (
        "主機螢幕組合模式",
        "主机屏幕组合模式",
        "Host display arrangement",
    ),
    ("串流螢幕類型", "串流屏幕类型", "Host display type"),
    (
        "最佳化主機遊戲設定",
        "优化主机游戏设置",
        "Optimize host game settings",
    ),
    (
        "結束串流時關閉主機程式",
        "结束串流时关闭主机程序",
        "Quit host app after streaming",
    ),
    (
        "遠端桌面滑鼠模式",
        "远程桌面鼠标模式",
        "Remote desktop mouse mode",
    ),
    ("顯示本機游標", "显示本机光标", "Show local cursor"),
    (
        "攔截系統快捷鍵",
        "拦截系统快捷键",
        "Capture system shortcuts",
    ),
    ("交換滑鼠左右鍵", "交换鼠标左右键", "Swap mouse buttons"),
    (
        "交換 Alt 與 Win 鍵",
        "交换 Alt 与 Win 键",
        "Swap Alt and Win keys",
    ),
    ("反轉捲動方向", "反转滚动方向", "Reverse scrolling"),
    (
        "觸控螢幕使用虛擬觸控板",
        "触摸屏使用虚拟触控板",
        "Touchscreen as virtual touchpad",
    ),
    (
        "使用精確式觸控板輸入",
        "使用精确式触控板输入",
        "Use native precision touchpad",
    ),
    (
        "交換 A／B 與 X／Y",
        "交换 A／B 与 X／Y",
        "Swap A/B and X/Y buttons",
    ),
    ("搖桿死區（%）", "摇杆死区（%）", "Stick deadzone (%)"),
    (
        "結束串流手把組合鍵",
        "结束串流手柄组合键",
        "Gamepad disconnect shortcut",
    ),
    (
        "強制第一支手把保持連線",
        "强制第一只手柄保持连接",
        "Keep first gamepad connected",
    ),
    (
        "長按 Start 使用手把滑鼠",
        "长按 Start 使用手柄鼠标",
        "Hold Start for gamepad mouse",
    ),
    (
        "背景接收手把輸入",
        "后台接收手柄输入",
        "Receive gamepad input in background",
    ),
    (
        "DualSense 進階觸覺",
        "DualSense 高级触觉",
        "DualSense haptics",
    ),
    (
        "串流時顯示效能統計",
        "串流时显示性能统计",
        "Show performance statistics",
    ),
    (
        "串流控制選單位置",
        "串流控制菜单位置",
        "Stream control menu position",
    ),
    ("啟用 YUV 4:4:4", "启用 YUV 4:4:4", "Enable YUV 4:4:4"),
    (
        "顯示連線品質警告",
        "显示连接质量警告",
        "Show connection warnings",
    ),
    (
        "顯示設定警告",
        "显示设置警告",
        "Show configuration warnings",
    ),
    (
        "串流時保持螢幕喚醒",
        "串流时保持屏幕唤醒",
        "Keep display awake while streaming",
    ),
    (
        "啟動時的視窗模式",
        "启动时的窗口模式",
        "Launcher startup window mode",
    ),
    (
        "記住視窗位置與大小",
        "记住窗口位置与大小",
        "Remember window position and size",
    ),
    ("HDR 亮度設定", "HDR 亮度设置", "HDR brightness"),
    (
        "峰值亮度（nits）",
        "峰值亮度（nits）",
        "Peak luminance (nits)",
    ),
    (
        "最低亮度（nits）",
        "最低亮度（nits）",
        "Minimum luminance (nits)",
    ),
    (
        "全畫面亮度（nits）",
        "全画面亮度（nits）",
        "Full-frame luminance (nits)",
    ),
    (
        "客戶端螢幕編號（0 為跟隨視窗）",
        "客户端屏幕编号（0 为跟随窗口）",
        "Client monitor number (0 follows launcher)",
    ),
    ("自訂寬度", "自定义宽度", "Custom width"),
    ("自訂高度", "自定义高度", "Custom height"),
    ("自訂 FPS", "自定义 FPS", "Custom FPS"),
    (
        "主機螢幕識別碼（留空跟隨主機）",
        "主机屏幕标识码（留空跟随主机）",
        "Host display ID (empty follows host)",
    ),
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
