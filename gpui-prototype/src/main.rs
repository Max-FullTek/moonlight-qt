#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod core;
mod input;
mod settings;
mod ui;
use gpui::{prelude::*, *};
use settings::Settings;
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
enum Field {
    Resolution,
    Fps,
    Codec,
    Fec,
}
#[derive(Clone)]
enum Action {
    Devices,
    Device(usize),
    Settings,
    About,
    Add,
    Close,
    SubmitAddress,
    Stop,
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
}
enum Modal {
    Info(&'static str, String),
    Add,
    Pair(String),
    Stream,
    Select(Field),
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
    selected: Option<String>,
    address: String,
    pending: bool,
    streaming: bool,
    stage: String,
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
            selected: None,
            address: String::new(),
            pending: false,
            streaming: false,
            stage: String::new(),
            slider_bounds: Rc::new(Cell::new(Bounds::default())),
            dragging: false,
            root_focus: cx.focus_handle(),
            modal_focus: cx.focus_handle(),
        }
    }
    fn act(&mut self, action: Action, window: &mut Window, cx: &mut Context<Self>) {
        let had_modal = self.modal.is_some();
        match action {
            Action::Devices => self.page = Page::Devices,
            Action::Settings => self.page = Page::Settings,
            Action::Device(_) if self.pending || self.streaming => {}
            Action::Device(index) => {
                if let Some(host) = self.hosts.get(index).cloned() {
                    self.selected = Some(host.id.clone());
                    if !host.online {
                        self.modal = Some(Modal::Info("主機目前離線", "請確認主機與 Tailscale 網路，再重新選取裝置。".into()));
                    } else if host.paired { self.page = Page::Apps; }
                    else if self.request(serde_json::json!({"command":"pair","host":host.id})) {
                        self.pending = true;
                        self.modal = Some(Modal::Info("正在配對", "正在向主機要求配對 PIN…".into()));
                    }
                }
            }
            Action::About => self.modal = Some(Modal::Info("關於 GPUI 整合版",
                "Rust + GPUI 原生啟動器；C++ 核心暫時保留 Qt 後端及 SDL/D3D11 串流。核心模式不載入 QML。此階段先整合手動主機、配對、程式清單與視窗串流；其餘設定尚未移植。".into())),
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
                if self.pending || self.streaming { self.notice = "操作仍在進行；點選裝置前請等待完成。".into(); }
                self.modal = None;
            },
            Action::Stop => { self.request(serde_json::json!({"command":"stop"})); }
            Action::Stream(app) => {
                if !self.pending && !self.streaming {
                    if let Some(host) = self.selected.clone() {
                        if self.request(serde_json::json!({"command":"start","host":host,"app":app,"settings":core::stream_settings(&self.saved)})) {
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
        if self.modal.is_some() {
            window.focus(&self.modal_focus);
        } else if had_modal {
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
                    self.notice = "主機已新增，請點選卡片配對。".into();
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
                    self.stage = "串流進行中；關閉 SDL 視窗或按下方按鈕結束連線。".into();
                    self.modal = Some(Modal::Stream);
                    window.minimize_window();
                }
                core::Event::StreamFinished => {
                    self.pending = false;
                    self.streaming = false;
                    if matches!(self.modal, Some(Modal::Stream)) {
                        self.modal = None;
                    }
                    self.notice = "串流已結束；主機上的程式保持開啟。".into();
                    window.activate_window();
                }
                core::Event::Error { code, message } => {
                    self.pending = false;
                    if matches!(code.as_str(), "core_exited" | "protocol" | "pipe_write") {
                        self.ready = false;
                        self.streaming = false;
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
                if matches!(this.modal, Some(Modal::Add)) && this.modal_focus.is_focused(window) {
                    let key = &event.keystroke;
                    if key.key == "backspace" { this.address.pop(); }
                    else if key.modifiers.control && key.key == "v" {
                        if let Some(text) = cx.read_from_clipboard().and_then(|item| item.text()) {
                            this.address = text.trim().chars().filter(|c| c.is_ascii_graphic()).take(260).collect();
                        }
                    } else if key.modifiers.control && key.key == "a" { this.address.clear(); }
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
                    this.modal = None;
                    window.focus(&this.root_focus);
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
                    .child("GPUI 核心整合測試 · 手動連線 · SDL 串流"),
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
                    .when(self.streaming, |d| d.child(self.button("header-stop", "結束連線", Action::Stop, false, cx)))
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
                        .child(if self.notice.is_empty() { "區網自動搜尋已關閉。使用手動 IP／Tailscale；串流暫以主螢幕視窗模式啟動。".to_owned() } else { self.notice.clone() }),
                )
            })
            .when(self.modal.is_some(), |d| d.child(self.modal(cx)))
    }
}
fn main() {
    Application::new().with_assets(Assets).run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(1280.), px(800.)), cx);
        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(bounds)),
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
