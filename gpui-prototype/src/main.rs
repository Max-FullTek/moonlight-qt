#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
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
    Apps(usize),
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
    Pair,
    Stream,
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
    Info(&'static str, &'static str),
    Pair,
    Stream,
    Select(Field),
}
struct Prototype {
    page: Page,
    modal: Option<Modal>,
    draft: Settings,
    saved: Settings,
    notice: String,
    paired: bool,
    slider_bounds: Rc<Cell<Bounds<Pixels>>>,
    dragging: bool,
    root_focus: FocusHandle,
    modal_focus: FocusHandle,
}
const HOSTS: &[(&str, &str, &str)] = &[
    ("工作電腦", "100.64.0.10", "已配對 · 在線"),
    ("遊戲主機", "192.168.1.20", "尚未配對"),
    ("客廳電腦", "192.168.1.30", "離線"),
];
impl Prototype {
    fn new(cx: &mut Context<Self>) -> Self {
        let (saved, notice) = settings::load();
        Self {
            page: Page::Devices,
            modal: None,
            draft: saved.clone(),
            saved,
            notice,
            paired: false,
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
            Action::Device(2) => {
                self.modal = Some(Modal::Info(
                    "主機目前離線",
                    "這是離線範例。初版尚未加入網路連線或喚醒功能。",
                ))
            }
            Action::Device(1) if !self.paired => self.modal = Some(Modal::Pair),
            Action::Device(index) => self.page = Page::Apps(index),
            Action::Pair => {
                self.paired = true;
                self.page = Page::Apps(1);
                self.modal = None;
            }
            Action::About => {
                self.modal = Some(Modal::Info(
                    "關於 GPUI 初版",
                    "Rust + GPUI 0.2.2 原生介面。沒有 Qt、瀏覽器或 WebView。僅裝置首頁與串流設定初版；所有主機與串流操作均為範例，未接入串流核心。",
                ))
            }
            Action::Add => {
                self.modal = Some(Modal::Info(
                    "新增裝置尚未實作",
                    "此初版先確認裝置卡片、配對流程與串流設定的呈現。手動輸入 IP、文字輸入法及真實主機管理列為下一步。",
                ))
            }
            Action::Close => self.modal = None,
            Action::Stream => self.modal = Some(Modal::Stream),
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
                    self.notice = "已儲存 GPUI 原型設定；不影響 Moonlight。".into();
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
                    .child("GPUI 原生初版 · 不會連線真實主機"),
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
                    .when(!narrow, |d| {
                        d.child(self.button("about", "關於", Action::About, false, cx))
                    }),
            )
            .child(match self.page {
                Page::Devices => self.devices(cx),
                Page::Apps(i) => self.apps(i, cx),
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
                        .child("區網自動搜尋已關閉。此初版使用獨立範例資料。"),
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
                    title: Some("Moonlight V+ Lite — GPUI Prototype".into()),
                    ..Default::default()
                }),
                ..Default::default()
            },
            |window, cx| {
                let view = cx.new(Prototype::new);
                window.focus(&view.read(cx).root_focus);
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
