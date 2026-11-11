#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod advanced;
mod core;
mod profile_identity;
mod host_api;
mod monitor;
mod controls;
mod radial;
mod stream_window;
mod i18n;
mod input;

#[cfg(not(target_os = "windows"))]
compile_error!("This frontend targets Windows PC only.");
mod settings;
mod ui;
use gpui::{prelude::*, *};
use settings::Settings;
use controls::{ControlAction, Controls};
use std::{borrow::Cow, cell::Cell, rc::Rc};

pub const BG: u32 = 0x10151d;
pub const SURFACE: u32 = 0x171f28;
pub const BORDER: u32 = 0x2b3541;
pub const TEXT: u32 = 0xedf3f8;
pub const MUTED: u32 = 0x9aaaba;
pub const ACCENT: u32 = 0x39c5bb;
struct Assets;
impl AssetSource for Assets {
    fn load(&self, path: &str) -> anyhow::Result<Option<Cow<'static, [u8]>>> {
        Ok(match path {
            "monitor" => Some(Cow::Borrowed(include_bytes!("../assets/monitor.svg"))),
            "sliders" => Some(Cow::Borrowed(include_bytes!("../assets/sliders.svg"))),
            "gamepad" => Some(Cow::Borrowed(include_bytes!("../assets/gamepad.svg"))),
            _ => None,
        })
    }
    fn list(&self, _: &str) -> anyhow::Result<Vec<SharedString>> {
        Ok(vec![])
    }
}
#[derive(Clone, Copy, PartialEq)]
enum Page {
    Devices,
    Apps,
    Settings,
}
#[derive(Clone, Copy, PartialEq)]
enum SettingsTab {
    Stream,
    Display,
    Interface,
    Audio,
    Host,
    Input,
    Gamepad,
    Compatibility,
}
impl SettingsTab {
    fn category(self) -> &'static str {
        match self {
            Self::Stream => "stream",
            Self::Display => "display",
            Self::Interface => "interface",
            Self::Audio => "audio",
            Self::Host => "host",
            Self::Input => "input",
            Self::Gamepad => "gamepad",
            Self::Compatibility => "compatibility",
        }
    }
}
const TABS: &[(SettingsTab, &str)] = &[
    (SettingsTab::Stream, "串流"),
    (SettingsTab::Display, "顯示"),
    (SettingsTab::Audio, "音效"),
    (SettingsTab::Host, "主機"),
    (SettingsTab::Input, "鍵盤與滑鼠"),
    (SettingsTab::Gamepad, "手把"),
    (SettingsTab::Compatibility, "相容性與診斷"),
    (SettingsTab::Interface, "介面"),
];
#[derive(Clone, Copy, PartialEq)]
enum Field {
    Resolution,
    Fps,
    Codec,
    Fec,
    WindowMode,
    Decoder,
    Language,
}
#[derive(Clone)]
enum Action {
    Devices,
    Device(usize),
    OpenApps(usize),
    ShowHidden,
    AppOptions(i32),
    AppPreferences(i32, bool, bool),
    Settings,
    SettingsTab(SettingsTab),
    Hdr,
    Vsync,
    FramePacing,
    About,
    Add,
    Close,
    SubmitAddress,
    Stop,
    OpenControls,
    Control(ControlAction),
    Stream(i32),
    Select(Field),
    Choose(Field, usize),
    Abr,
    Enhance,
    Scale,
    Bitrate(i32),
    FecPercent(i32),
    ScalePercent(i32),
    Recommend,
    Defaults,
    Save,
    Advanced(&'static str),
    SetAdvanced(&'static str, serde_json::Value),
    SubmitValue(&'static str),
    FetchDisplays,
    HostOptions(String),
    RenameHost(String),
    SubmitHostName(String),
    ConfirmRemove(String),
    RemoveHost(String),
    ConfirmQuit,
    QuitHostApp,
}
enum Modal {
    Info(&'static str, String),
    Add,
    Pair(String),
    Stream,
    Select(Field),
    AppOptions(i32),
    Advanced(&'static str),
    EditValue(&'static str),
    HostOptions(String),
    RenameHost(String),
    ConfirmRemove(String),
    ConfirmQuit,
}
struct Prototype {
    page: Page,
    modal: Option<Modal>,
    draft: Settings,
    saved: Settings,
    notice: String,
    core: Option<core::Client>,
    ready: bool,
    hosts: Vec<core::Host>,
    host_displays: Vec<core::HostDisplay>,
    selected: Option<String>,
    address: String,
    input_marked: Option<std::ops::Range<usize>>,
    pending: bool,
    streaming: bool,
    controls: Controls,
    control_menu: Option<WindowHandle<radial::Menu>>,
    stage: String,
    settings_tab: SettingsTab,
    show_hidden: bool,
    slider_bounds: Rc<Cell<Bounds<Pixels>>>,
    dragging: bool,
    root_focus: FocusHandle,
    modal_focus: FocusHandle,
}
impl Prototype {
    fn new(cx: &mut Context<Self>) -> Self {
        let (saved, notice) = settings::load();
        let core = core::Client::spawn();
        let (client, notice) = match core {
            Ok(client) => (Some(client), notice),
            Err(error) => (None, format!("無法啟動核心：{error}")),
        };
        Self {
            page: Page::Devices,
            modal: None,
            draft: saved.clone(),
            saved,
            notice,
            core: client,
            ready: false,
            hosts: vec![],
            host_displays: vec![],
            selected: None,
            address: String::new(),
            input_marked: None,
            pending: false,
            streaming: false,
            controls: Controls::default(),
            control_menu: None,
            stage: String::new(),
            settings_tab: SettingsTab::Stream,
            show_hidden: false,
            slider_bounds: Rc::new(Cell::new(Bounds::default())),
            dragging: false,
            root_focus: cx.focus_handle(),
            modal_focus: cx.focus_handle(),
        }
    }
    fn tr(&self, text: &str) -> SharedString {
        i18n::text(self.draft.language, text).into()
    }
    fn act(&mut self, action: Action, window: &mut Window, cx: &mut Context<Self>) {
        let had_modal = self.modal.is_some();
        self.input_marked = None;
        match action {
            Action::HostOptions(host) => self.modal = Some(Modal::HostOptions(host)),
            Action::RenameHost(host) => {
                if let Some(h) = self.hosts.iter().find(|h| h.id == host) {
                    self.address = h.name.clone(); self.modal = Some(Modal::RenameHost(host));
                }
            }
            Action::SubmitHostName(host) => {
                let name = self.address.trim().to_owned();
                if name.is_empty() || name.encode_utf16().count() > 128 || name.chars().any(char::is_control) {
                    self.notice = "主機名稱須為 1～128 個字元。".into();
                } else if !self.pending && !self.streaming && self.request(serde_json::json!({"command":"rename","host":host,"name":name})) { self.pending = true; }
            }
            Action::ConfirmRemove(host) => self.modal = Some(Modal::ConfirmRemove(host)),
            Action::RemoveHost(host) => {
                if !self.pending && !self.streaming && self.request(serde_json::json!({"command":"remove","host":host})) { self.pending = true; }
            }
            Action::ConfirmQuit => self.modal = Some(Modal::ConfirmQuit),
            Action::QuitHostApp => {
                if let Some(host) = self.selected.clone() {
                    if !self.pending && !self.streaming && self.request(serde_json::json!({"command":"quit","host":host})) { self.pending = true; self.modal = Some(Modal::Info("正在結束程式", "等待主機結束程式…".into())); }
                }
            }
            Action::FetchDisplays => {
                if !self.pending && !self.streaming {
                    if let Some(host) = self.selected.clone() {
                        if self.request(serde_json::json!({"command":"displays","host":host})) { self.pending=true; self.notice="正在讀取主機螢幕清單…".into(); }
                    } else { self.notice="請先在裝置頁選取已配對的主機。".into(); }
                }
            }
            Action::Advanced(key) => {
                let spec=advanced::spec(key).unwrap();
                if let advanced::Kind::Toggle(_) = spec.kind {
                    let value=!self.draft.option(key).as_bool().unwrap();
                    self.draft.advanced.insert(key.into(),serde_json::json!(value));
                } else if matches!(spec.kind,advanced::Kind::Choice(..)) { self.modal=Some(Modal::Advanced(key)); }
                else { self.address= match self.draft.option(key) { serde_json::Value::String(s)=>s,v=>v.to_string() };self.modal=Some(Modal::EditValue(key)); }
                self.notice.clear();
            }
            Action::SetAdvanced(key,value) => {
                if advanced::spec(key).is_some_and(|s| s.accepts(&value)) { self.draft.advanced.insert(key.into(),value);self.modal=None;self.notice.clear(); }
            }
            Action::SubmitValue(key) => {
                let spec=advanced::spec(key).unwrap();
                let value=if matches!(spec.kind,advanced::Kind::Text) { serde_json::Value::String(self.address.trim().into()) }
                  else { self.address.trim().parse::<f64>().ok().filter(|v|v.is_finite()).map(serde_json::Value::from).unwrap_or(serde_json::Value::Null) };
                if spec.accepts(&value) { self.draft.advanced.insert(key.into(),value);self.modal=None;self.notice.clear(); }
                else { self.notice="輸入值超出有效範圍。".into(); }
            }
            Action::Devices => self.page = Page::Devices,
            Action::Settings => self.page = Page::Settings,
            Action::SettingsTab(tab) => { self.settings_tab = tab; }
            Action::Hdr => { self.draft.hdr = !self.draft.hdr; self.notice.clear(); }
            Action::Vsync => { self.draft.vsync = !self.draft.vsync; self.notice.clear(); }
            Action::FramePacing => { self.draft.frame_pacing = !self.draft.frame_pacing; self.notice.clear(); }
            Action::ShowHidden => self.show_hidden = !self.show_hidden,
            Action::OpenApps(index) => {
                if let Some(host) = self.hosts.get(index) {
                    if self.selected.as_ref() != Some(&host.id) { self.host_displays.clear(); }
                    self.selected = Some(host.id.clone()); self.page = Page::Apps;
                }
            }
            Action::AppOptions(id) => self.modal = Some(Modal::AppOptions(id)),
            Action::AppPreferences(app, hidden, direct_launch) => {
                if let Some(host) = self.selected.clone() {
                    if self.request(serde_json::json!({"command":"app_preferences","host":host,"app":app,"hidden":hidden,"direct_launch":direct_launch})) { self.modal = None; }
                }
            }
            Action::Device(_) if self.pending || self.streaming => {}
            Action::Device(index) => {
                if let Some(host) = self.hosts.get(index).cloned() {
                    if self.selected.as_ref() != Some(&host.id) { self.host_displays.clear(); }
                    self.selected = Some(host.id.clone());
                    if !host.online {
                        self.modal = Some(Modal::Info("主機目前離線", "請確認主機與 Tailscale 網路，再重新選取裝置。".into()));
                    } else if host.paired {
                        self.page = Page::Apps;
                        if let Some(app) = host.direct_app() { self.act(Action::Stream(app), window, cx); }
                    }
                    else if self.request(serde_json::json!({"command":"pair","host":host.id})) {
                        self.pending = true;
                        self.modal = Some(Modal::Info("正在配對", "正在向主機要求配對 PIN…".into()));
                    }
                }
            }
            Action::About => self.modal = Some(Modal::Info("關於 GPUI 整合版",
                "Windows PC 客戶端：GPUI 介面搭配 C++／SDL 串流核心。僅手動連線；不提供 USB/IP、檔案傳送或自動下載更新。".into())),
            Action::Add => {
                if self.ready && !self.pending {
                    self.address.clear(); self.modal = Some(Modal::Add);
                } else { self.notice = "核心尚未就緒，或目前仍有操作進行中。".into(); }
            }
            Action::SubmitAddress => {
                let address = self.address.trim().to_owned();
                if address.is_empty() || address.len() > 260 || address.chars().any(|c| c.is_whitespace() || "/\\?#@".contains(c)) {
                    self.notice = "請輸入 IP 或主機名稱，可附連接埠；不要輸入網址。".into();
                } else if self.request(serde_json::json!({"command":"add","address":address})) {
                    self.pending = true;
                    self.modal = Some(Modal::Info("正在連線", "正在取得主機資訊…".into()));
                }
            }
            Action::Close => {
                if self.controls.active {
                    self.act(Action::Control(ControlAction::Resume), window, cx);
                    return;
                }
                if self.pending || self.streaming { self.notice = "操作仍在進行；點選裝置前請等待完成。".into(); }
                self.modal = None;
            },
            Action::Stop => { self.request(serde_json::json!({"command":"stop"})); }
            Action::OpenControls => { self.request(serde_json::json!({"command":"controls","action":"open"})); }
            Action::Control(action) => {
                self.controls.selected = self.controls.order.iter().position(|&item| item == action).unwrap_or(0);
                if !self.pending && self.controls.choose(action) {
                    let mut request = serde_json::json!({"command":"controls","action":action.command()});
                    if matches!(action, ControlAction::BitrateDown | ControlAction::BitrateUp) {
                        let step = if action == ControlAction::BitrateDown { -1 } else { 1 };
                        let bitrate = (self.controls.bitrate as i32 + step).clamp(1, 150) as u32;
                        request["value"] = serde_json::json!(bitrate * 1000);
                        request["request_id"] = serde_json::json!(self.controls.bitrate_feedback.next_id());
                        if self.request(request) {
                            self.controls.bitrate = bitrate;
                            self.controls.bitrate_feedback.request(bitrate * 1000);
                        }
                    } else if self.request(request) && matches!(action, ControlAction::Disconnect | ControlAction::End) {
                        self.pending = true;
                    }
                }
            }
            Action::Stream(app) => {
                if !self.pending && !self.streaming {
                    if let Some(host) = self.selected.clone() {
                        if self.request(serde_json::json!({"command":"start","client_pid":std::process::id(),"host":host,"app":app,"settings":core::stream_settings(&self.saved),"client_center":{"x":f32::from(window.bounds().center().x)*window.scale_factor(),"y":f32::from(window.bounds().center().y)*window.scale_factor()}})) {
                            self.controls = Controls { bitrate: self.saved.bitrate, order: self.saved.control_order, ..Default::default() };
                            self.pending = true;
                            self.stage = "正在啟動串流…".into();
                            self.modal = Some(Modal::Stream);
                        }
                    }
                }
            }
            Action::Select(field) => self.modal = Some(Modal::Select(field)),
            Action::Choose(field, value) => {
                match field {
                    Field::Resolution => self.draft.resolution = value,
                    Field::Fps => self.draft.fps = value,
                    Field::Codec => self.draft.codec = value,
                    Field::Fec => self.draft.fec = value,
                    Field::WindowMode => self.draft.window_mode = value,
                    Field::Decoder => self.draft.decoder = value,
                    Field::Language => self.draft.language = value,
                }
                self.modal = None;
                self.notice.clear();
            }
            Action::Abr => {
                self.draft.abr = !self.draft.abr;
                self.notice.clear();
            }
            Action::Enhance => {
                self.draft.enhancement = !self.draft.enhancement;
                self.notice.clear();
            }
            Action::Scale => {
                self.draft.scale = !self.draft.scale;
                self.notice.clear();
            }
            Action::Bitrate(d) => {
                self.draft.bitrate = (self.draft.bitrate as i32 + d).clamp(1, 150) as u32;
                self.notice.clear();
            }
            Action::FecPercent(d) => {
                self.draft.fec_percent = (self.draft.fec_percent as i32 + d).clamp(0, 100) as u32;
                self.notice.clear();
            }
            Action::ScalePercent(d) => {
                self.draft.scale_percent =
                    (self.draft.scale_percent as i32 + d).clamp(20, 100) as u32;
                self.notice.clear();
            }
            Action::Recommend => {
                self.draft.bitrate = self.draft.recommended_bitrate();
                self.notice.clear();
            }
            Action::Defaults => {
                self.draft = Settings::default();
                self.notice = "已載入預設值，按儲存後保留。".into();
            }
            Action::Save => match settings::save(&self.draft) {
                Ok(()) => {
                    self.saved = self.draft.clone();
                    self.notice = "已儲存；下次串流使用此設定。".into();
                }
                Err(err) => self.notice = format!("無法儲存：{err}"),
            },
        }
        if self.control_menu.is_none() && self.modal.is_some() {
            window.focus(&self.modal_focus);
        } else if self.control_menu.is_none() && had_modal {
            window.focus(&self.root_focus);
        }
        cx.notify();
    }
    fn request(&mut self, value: serde_json::Value) -> bool {
        let result = self
            .core
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("核心未啟動"))
            .and_then(|client| client.send(value));
        match result {
            Ok(()) => true,
            Err(error) => {
                self.modal = Some(Modal::Info("核心錯誤", error.to_string()));
                false
            }
        }
    }
    fn start_polling(&self, window: &Window, cx: &mut Context<Self>) {
        cx.spawn_in(window, async move |weak, cx| {
            loop {
                Timer::after(std::time::Duration::from_millis(100)).await;
                if cx
                    .update(|window, cx| weak.update(cx, |view, cx| view.poll_core(window, cx)))
                    .is_err()
                {
                    break;
                }
            }
        })
        .detach();
    }
    fn poll_core(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        settings::remember_bounds(&self.saved, window);
        let events = self
            .core
            .as_ref()
            .map(|client| client.drain())
            .unwrap_or_default();
        if events.is_empty() {
            return;
        }
        let had_modal = self.modal.is_some();
        for event in events {
            match event {
                core::Event::HostApiContext { .. } | core::Event::PairContext { .. } | core::Event::MonitorContexts { .. } | core::Event::QuitContext { .. } | core::Event::SessionApi { .. } | core::Event::ProfileIdentity { .. } => unreachable!("private worker context must be handled before the UI"),
                core::Event::HostRenamed => {
                    self.pending = false;
                    self.modal = None;
                    self.notice = "主機名稱已更新。".into();
                }
                core::Event::HostRemoved { host } => {
                    self.pending = false;
                    self.modal = None;
                    self.notice = "已移除此客戶端的主機記錄。".into();
                    if self.selected.as_ref() == Some(&host) {
                        self.selected = None;
                        self.host_displays.clear();
                        self.page = Page::Devices;
                    }
                }
                core::Event::AppQuit => {
                    self.pending = false;
                    self.modal = None;
                    self.notice = "主機程式已結束。".into();
                }
                core::Event::Displays { host, displays, .. } => {
                    self.pending = false;
                    if Some(&host) == self.selected.as_ref() {
                        self.host_displays = displays;
                        self.notice = if self.host_displays.is_empty() {
                            "主機未回報螢幕，可跟隨主機設定或手動輸入識別碼。"
                        } else {
                            "已讀取主機螢幕清單。"
                        }
                        .into();
                    }
                }
                core::Event::Ready { .. } => {
                    self.ready = true;
                    self.notice = "核心已就緒，請新增或選取裝置。".into();
                }
                core::Event::Hosts { hosts } => {
                    self.hosts = hosts;
                }
                core::Event::Added => {
                    self.pending = false;
                    self.modal = None;
                    self.notice = "主機已新增，請按「開始 PIN 碼配對」。".into();
                }
                core::Event::Pairing { host, pin } => {
                    self.selected = Some(host);
                    self.modal = Some(Modal::Pair(pin));
                }
                core::Event::Paired => {
                    self.pending = false;
                    self.modal = None;
                    self.page = Page::Apps;
                    self.notice = "配對成功。".into();
                }
                core::Event::Launching => {
                    self.stage = "正在啟動串流…".into();
                }
                core::Event::Stage { stage } => {
                    self.stage = stage;
                }
                core::Event::StreamStarted => {
                    self.pending = false;
                    self.streaming = true;
                    self.stage = "串流進行中；Ctrl+Alt+Shift+O 開啟控制頁。".into();
                    self.modal = Some(Modal::Stream);
                    window.minimize_window();
                }
                core::Event::StreamFinished => {
                    self.pending = false;
                    self.streaming = false;
                    self.close_control_menu(cx);
                    self.controls = Controls::default();
                    if matches!(self.modal, Some(Modal::Stream)) {
                        self.modal = None;
                    }
                    self.notice = "串流已結束。".into();
                    window.activate_window();
                }
                core::Event::Controls { active, gamepad_mouse, has_gamepad } => {
                    let opening = active && !self.controls.active;
                    self.controls.active = active;
                    self.controls.gamepad_mouse = gamepad_mouse;
                    self.controls.has_gamepad = has_gamepad;
                    if opening {
                        self.controls.selected = 0;
                        self.controls.confirm_end = false;
                        self.controls.editing = false;
                        self.controls.moving = None;
                        self.open_control_menu(cx);
                    } else if !active {
                        self.close_control_menu(cx);
                        self.modal = Some(Modal::Stream);
                    }
                }
                core::Event::BitrateResult { request_id, requested_kbps, success, message } => {
                    if self.streaming {
                        self.controls.bitrate_feedback.result(request_id, requested_kbps, success, message);
                    }
                }
                core::Event::ControlsNavigate { direction } => {
                    if self.controls.active && self.control_menu.is_some_and(|menu|
                        menu.update(cx, |_, window, _| window.is_window_active()).unwrap_or(false)) {
                        self.navigate_controls(&direction, window, cx);
                    }
                }
                core::Event::Error { code, message } => {
                    self.pending = false;
                    if matches!(code.as_str(), "core_exited" | "protocol" | "pipe_write") {
                        self.ready = false;
                        self.streaming = false;
                        self.close_control_menu(cx);
                        self.controls = Controls::default();
                        self.core = None;
                        window.activate_window();
                    }
                    self.modal = Some(Modal::Info("操作失敗", format!("{message} ({code})")));
                }
            }
        }
        if self.modal.is_some() && !had_modal {
            window.focus(&self.modal_focus);
        } else if had_modal && self.modal.is_none() {
            window.focus(&self.root_focus);
        }
        cx.notify();
    }
    fn close_control_menu(&mut self, cx: &mut Context<Self>) {
        if let Some(menu) = self.control_menu.take() {
            let _ = menu.update(cx, |_, window, _| window.remove_window());
        }
    }
    fn open_control_menu(&mut self, cx: &mut Context<Self>) {
        let Some(pid) = self.core.as_ref().map(core::Client::process_id) else { return };
        let owner = cx.entity();
        let state = self.controls.clone();
        let result = cx.open_window(WindowOptions {
            kind: WindowKind::PopUp,
            titlebar: None,
            window_bounds: Some(WindowBounds::Windowed(Bounds::new(point(px(0.), px(0.)), size(px(680.), px(680.))))),
            window_background: WindowBackgroundAppearance::Transparent,
            is_movable: false, is_resizable: false, is_minimizable: false,
            focus: false, show: true,
            ..Default::default()
        }, |window, cx| {
            window.set_window_title("Moonlight 串流控制");
            cx.new(|cx| radial::Menu::new(owner, state, window, cx))
        });
        let result = result.and_then(|menu| {
            let attached = menu.update(cx, |_, window, _| stream_window::attach(window, pid))?;
            if let Err(error) = attached {
                let _ = menu.update(cx, |_, window, _| window.remove_window());
                return Err(error);
            }
            menu.update(cx, |_, window, _| window.activate_window())?;
            Ok(menu)
        });
        match result {
            Ok(menu) => self.control_menu = Some(menu),
            Err(error) => { self.notice = format!("無法開啟串流選單：{error}");
                self.request(serde_json::json!({"command":"controls","action":"resume"})); }
        }
    }
    fn navigate_controls(&mut self, direction: &str, window: &mut Window, cx: &mut Context<Self>) {
        match direction {
            "up" | "down" | "left" | "right" => self.controls.navigate(direction),
            "previous" | "next" => self.controls.move_selection(direction == "next"),
            "edit" => {
                self.controls.editing = !self.controls.editing;
                self.controls.moving = None;
                self.controls.confirm_end = false;
            }
            "select" if self.controls.editing => {
                if let Some(source) = self.controls.moving.take() {
                    self.controls.order.swap(source, self.controls.selected);
                    let mut settings = self.saved.clone();
                    settings.control_order = self.controls.order;
                    match settings::save(&settings) {
                        Ok(()) => { self.saved = settings; self.draft.control_order = self.controls.order; }
                        Err(error) => { self.controls.order.swap(source, self.controls.selected);
                            self.controls.bitrate_feedback.error = Some(format!("無法儲存圖示順序：{error}")); }
                    }
                } else { self.controls.moving = Some(self.controls.selected); }
            }
            "select" => self.act(Action::Control(self.controls.order[self.controls.selected]), window, cx),
            "back" => {
                if self.controls.moving.take().is_some() {}
                else if self.controls.editing { self.controls.editing = false; }
                else if self.controls.confirm_end { self.controls.confirm_end = false; }
                else { self.act(Action::Control(ControlAction::Resume), window, cx); }
            }
            _ => {}
        }
        cx.notify();
    }
    fn set_bitrate_at(&mut self, x: Pixels) {
        let bounds = self.slider_bounds.get();
        if bounds.size.width > px(0.) {
            let ratio = ((x - bounds.origin.x) / bounds.size.width).clamp(0., 1.);
            self.draft.bitrate = (1. + ratio * 149.).round() as u32;
            self.notice.clear();
        }
    }
}
impl Render for Prototype {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let narrow = window.viewport_size().width < px(760.);
        div()
            .id("prototype-root")
            .track_focus(&self.root_focus)
            .tab_group()
            .tab_stop(false)
            .on_key_down(cx.listener(|this, event: &KeyDownEvent, window, cx| {
                if matches!(
                    this.modal,
                    Some(Modal::Add | Modal::EditValue(_) | Modal::RenameHost(_))
                ) && this.modal_focus.is_focused(window)
                {
                    let key = &event.keystroke;
                    if key.key == "backspace" {
                        this.address.pop();
                    } else if key.modifiers.control && key.key == "v" {
                        if let Some(text) = cx.read_from_clipboard().and_then(|item| item.text()) {
                            this.address = input::filter_text(text.trim(), this.unicode_input());
                        }
                    } else if key.modifiers.control && key.key == "a" {
                        this.address.clear();
                    }
                    cx.notify();
                }
                if event.keystroke.key == "tab" {
                    if event.keystroke.modifiers.shift {
                        window.focus_prev();
                    } else {
                        window.focus_next();
                    }
                    cx.stop_propagation();
                }
                if event.keystroke.key == "escape" && this.modal.is_some() {
                    if this.controls.active { this.act(Action::Close, window, cx); }
                    else { this.modal = None; window.focus(&this.root_focus); }
                    cx.notify();
                    cx.stop_propagation();
                }
            }))
            .size_full()
            .relative()
            .flex()
            .flex_col()
            .bg(rgb(BG))
            .text_color(rgb(TEXT))
            .font_family("Microsoft JhengHei UI")
            .text_size(px(14.))
            .child(
                div()
                    .h(px(36.))
                    .flex_shrink_0()
                    .px_6()
                    .flex()
                    .items_center()
                    .bg(rgb(0x0d1219))
                    .border_b_1()
                    .border_color(rgb(BORDER))
                    .text_size(px(12.))
                    .text_color(rgb(MUTED))
                    .child(self.tr("GPUI 核心整合測試 · 手動連線 · SDL 串流")),
            )
            .child(
                div()
                    .h(px(70.))
                    .flex_shrink_0()
                    .px_6()
                    .flex()
                    .items_center()
                    .gap_5()
                    .border_b_1()
                    .border_color(rgb(BORDER))
                    .child(svg().path("gamepad").size(px(27.)).text_color(rgb(TEXT)))
                    .child(
                        div()
                            .text_size(px(if narrow { 15. } else { 21. }))
                            .child("Moonlight V+ Lite"),
                    )
                    .child(self.navigation(
                        "devices-nav",
                        "裝置",
                        "monitor",
                        Action::Devices,
                        self.page != Page::Settings,
                        cx,
                    ))
                    .child(self.navigation(
                        "settings-nav",
                        "設定",
                        "sliders",
                        Action::Settings,
                        self.page == Page::Settings,
                        cx,
                    ))
                    .child(div().flex_1())
                    .when(self.streaming, |d| {
                        d.child(self.button("header-controls", "串流控制", Action::OpenControls, false, cx))
                    })
                    .when(!narrow, |d| {
                        d.child(self.button("about", "關於", Action::About, false, cx))
                    }),
            )
            .child(match self.page {
                Page::Devices => self.devices(cx),
                Page::Apps => self.apps(cx),
                Page::Settings => self.settings(narrow, cx),
            })
            .when(self.page != Page::Settings, |d| {
                d.child(
                    div()
                        .p_4()
                        .border_t_1()
                        .border_color(rgb(BORDER))
                        .text_size(px(12.))
                        .text_color(rgb(MUTED))
                        .child(self.tr(if self.notice.is_empty() {
                            "使用手動 IP／Tailscale 連線；串流依已儲存的顯示設定啟動。"
                        } else {
                            &self.notice
                        })),
                )
            })
            .when(self.modal.is_some(), |d| d.child(self.modal(cx)))
    }
}
impl Prototype {
    fn unicode_input(&self) -> bool {
        matches!(
            self.modal,
            Some(Modal::RenameHost(_) | Modal::EditValue("host_display"))
        )
    }
}
fn main() {
    Application::new().with_assets(Assets).run(|cx: &mut App| {
        let (startup, _) = settings::load();
        let bounds = settings::launcher_bounds(&startup)
            .filter(|b| cx.displays().iter().any(|d| d.bounds().intersects(b)))
            .unwrap_or_else(|| Bounds::centered(None, size(px(1280.), px(800.)), cx));
        let startup_bounds = match startup.option("ui_window").as_u64().unwrap() {
            1 => WindowBounds::Maximized(bounds),
            2 => WindowBounds::Fullscreen(bounds),
            _ => WindowBounds::Windowed(bounds),
        };
        cx.open_window(
            WindowOptions {
                window_bounds: Some(startup_bounds),
                window_min_size: Some(size(px(600.), px(500.))),
                titlebar: Some(TitlebarOptions {
                    title: Some("Moonlight V+ Lite — GPUI Integration".into()),
                    ..Default::default()
                }),
                ..Default::default()
            },
            |window, cx| {
                let view = cx.new(Prototype::new);
                window.focus(&view.read(cx).root_focus);
                view.update(cx, |this, cx| this.start_polling(window, cx));
                view
            },
        )
        .expect("Cannot open GPUI prototype window");
        cx.on_window_closed(|cx| {
            if cx.windows().is_empty() {
                cx.quit();
            }
        })
        .detach();
        cx.activate(true);
    });
}
