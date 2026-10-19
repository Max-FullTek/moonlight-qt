use crate::*;
use settings::{CODECS, FEC, FPS, RESOLUTIONS};

impl Prototype {
    pub fn button(
        &self,
        id: impl Into<ElementId>,
        label: impl Into<SharedString>,
        action: Action,
        primary: bool,
        cx: &Context<Self>,
    ) -> Stateful<Div> {
        div()
            .id(id)
            .tab_index(0)
            .cursor_pointer()
            .px_4()
            .py_2()
            .rounded(px(6.))
            .border_1()
            .border_color(rgb(if primary { ACCENT } else { 0x3a4858 }))
            .bg(rgb(if primary { ACCENT } else { BG }))
            .text_color(rgb(if primary { 0x09201f } else { TEXT }))
            .text_size(px(13.))
            .flex()
            .items_center()
            .justify_center()
            .hover(|s| s.bg(rgb(if primary { 0x5ed5cd } else { 0x1d2833 })))
            .focus(|s| s.border_color(rgb(ACCENT)))
            .on_click(cx.listener(move |this, _, window, cx| this.act(action.clone(), window, cx)))
            .child(label.into())
    }
    pub fn devices(&self, cx: &Context<Self>) -> AnyElement {
        div()
            .flex_1()
            .min_h_0()
            .id("devices-scroll")
            .overflow_y_scroll()
            .p_8()
            .child(
                div()
                    .flex()
                    .items_center()
                    .justify_between()
                    .mb_10()
                    .child(div().text_size(px(27.)).child("裝置"))
                    .child(self.button("add", "+ 新增裝置", Action::Add, false, cx)),
            )
            .child(
                div().flex().flex_wrap().justify_center().gap_7().children(
                    HOSTS
                        .iter()
                        .enumerate()
                        .map(|(i, (name, address, status))| {
                            let status = if i == 1 && self.paired {
                                "已配對 · 在線"
                            } else {
                                *status
                            };
                            div()
                                .id(("device", i))
                                .tab_index(0)
                                .cursor_pointer()
                                .w(px(240.))
                                .h(px(256.))
                                .rounded(px(6.))
                                .border_1()
                                .border_color(rgb(BORDER))
                                .bg(rgb(0x131b24))
                                .flex()
                                .flex_col()
                                .items_center()
                                .justify_center()
                                .gap_2()
                                .hover(|s| s.border_color(rgb(ACCENT)).bg(rgb(0x192730)))
                                .focus(|s| s.border_color(rgb(ACCENT)))
                                .on_click(cx.listener(move |this, _, window, cx| {
                                    this.act(Action::Device(i), window, cx)
                                }))
                                .child(
                                    svg()
                                        .path("monitor")
                                        .size(px(80.))
                                        .mb_4()
                                        .text_color(rgb(if i == 2 { MUTED } else { TEXT })),
                                )
                                .child(div().text_size(px(18.)).child(*name))
                                .child(
                                    div()
                                        .text_size(px(12.))
                                        .text_color(rgb(MUTED))
                                        .child(status),
                                )
                                .child(
                                    div()
                                        .text_size(px(12.))
                                        .text_color(rgb(MUTED))
                                        .child(*address),
                                )
                        }),
                ),
            )
            .into_any_element()
    }
    pub fn apps(&self, index: usize, cx: &Context<Self>) -> AnyElement {
        div()
            .flex_1()
            .min_h_0()
            .id("apps-scroll")
            .overflow_y_scroll()
            .p_8()
            .child(self.button("back", "‹ 返回裝置", Action::Devices, false, cx))
            .child(div().mt_6().mb_6().text_size(px(24.)).child(HOSTS[index].0))
            .children(
                [
                    ("桌面", "使用主機的完整桌面"),
                    ("Steam Big Picture", "以手把瀏覽遊戲收藏"),
                    ("Playnite", "開啟遊戲啟動器"),
                ]
                .into_iter()
                .enumerate()
                .map(|(i, (name, detail))| {
                    div()
                        .flex()
                        .items_center()
                        .gap_5()
                        .p_6()
                        .border_b_1()
                        .border_color(rgb(BORDER))
                        .child(
                            svg()
                                .path(if i == 0 { "monitor" } else { "gamepad" })
                                .size(px(36.)),
                        )
                        .child(
                            div()
                                .flex_1()
                                .child(div().text_size(px(18.)).child(name))
                                .child(
                                    div()
                                        .text_size(px(12.))
                                        .text_color(rgb(MUTED))
                                        .child(detail),
                                ),
                        )
                        .child(self.button(("start", i), "開始串流", Action::Stream, true, cx))
                }),
            )
            .into_any_element()
    }
    fn field(&self, label: &str, hint: &str, control: AnyElement, narrow: bool) -> Div {
        div()
            .flex()
            .when(narrow, |d| d.flex_col())
            .gap_4()
            .py_4()
            .border_b_1()
            .border_color(rgb(BORDER))
            .child(
                div()
                    .when(!narrow, |d| d.w(relative(0.42)))
                    .flex_shrink_0()
                    .child(label.to_owned())
                    .when(!hint.is_empty(), |d| {
                        d.child(
                            div()
                                .mt_1()
                                .text_size(px(12.))
                                .text_color(rgb(MUTED))
                                .child(hint.to_owned()),
                        )
                    }),
            )
            .child(div().flex_1().min_w_0().child(control))
    }
    fn select(&self, field: Field, cx: &Context<Self>) -> AnyElement {
        let (id, options, current) = match field {
            Field::Resolution => ("resolution", RESOLUTIONS, self.draft.resolution),
            Field::Fps => ("fps", FPS, self.draft.fps),
            Field::Codec => ("codec", CODECS, self.draft.codec),
            Field::Fec => ("fec", FEC, self.draft.fec),
        };
        self.button(
            id,
            format!("{}     ▾", options[current]),
            Action::Select(field),
            false,
            cx,
        )
        .w_full()
        .max_w(px(390.))
        .justify_between()
        .into_any_element()
    }
    fn toggle(
        &self,
        id: &'static str,
        value: bool,
        action: Action,
        cx: &Context<Self>,
    ) -> AnyElement {
        div()
            .id(id)
            .tab_index(0)
            .cursor_pointer()
            .w(px(42.))
            .h(px(24.))
            .rounded(px(20.))
            .p(px(3.))
            .bg(rgb(if value { ACCENT } else { 0x465361 }))
            .border_1()
            .border_color(rgb(BORDER))
            .focus(|s| s.border_color(rgb(TEXT)))
            .flex()
            .items_center()
            .when(value, |d| d.justify_end())
            .on_click(cx.listener(move |this, _, window, cx| this.act(action.clone(), window, cx)))
            .child(
                div()
                    .size(px(16.))
                    .rounded_full()
                    .bg(rgb(if value { 0x0a3431 } else { TEXT })),
            )
            .into_any_element()
    }
    fn bitrate(&self, cx: &Context<Self>) -> AnyElement {
        let bounds = self.slider_bounds.clone();
        div()
            .flex()
            .flex_wrap()
            .items_center()
            .gap_3()
            .child(
                div()
                    .id("bitrate-track")
                    .cursor_pointer()
                    .w(px(150.))
                    .h(px(28.))
                    .relative()
                    .on_mouse_down(
                        MouseButton::Left,
                        cx.listener(|this, event: &MouseDownEvent, _, cx| {
                            this.dragging = true;
                            this.set_bitrate_at(event.position.x);
                            cx.notify();
                        }),
                    )
                    .on_mouse_move(cx.listener(|this, event: &MouseMoveEvent, _, cx| {
                        if this.dragging {
                            this.set_bitrate_at(event.position.x);
                            cx.notify();
                        }
                    }))
                    .on_mouse_up(
                        MouseButton::Left,
                        cx.listener(|this, _, _, _| this.dragging = false),
                    )
                    .on_mouse_up_out(
                        MouseButton::Left,
                        cx.listener(|this, _, _, _| this.dragging = false),
                    )
                    .child(
                        canvas(
                            move |b, _, _| {
                                bounds.set(b);
                            },
                            |_, _, _, _| {},
                        )
                        .size_full()
                        .absolute(),
                    )
                    .child(
                        div()
                            .absolute()
                            .left_0()
                            .right_0()
                            .top(px(12.))
                            .h(px(4.))
                            .rounded_full()
                            .bg(rgb(0x465361))
                            .child(
                                div()
                                    .h_full()
                                    .w(relative(self.draft.bitrate as f32 / 150.))
                                    .bg(rgb(ACCENT)),
                            ),
                    )
                    .child(
                        div()
                            .absolute()
                            .top(px(7.))
                            .left(relative((self.draft.bitrate - 1) as f32 / 149. * 0.90))
                            .size(px(14.))
                            .rounded_full()
                            .bg(rgb(ACCENT)),
                    ),
            )
            .child(self.button("bitrate-minus", "−", Action::Bitrate(-1), false, cx))
            .child(
                div()
                    .text_size(px(13.))
                    .child(format!("{} Mbps", self.draft.bitrate)),
            )
            .child(self.button("bitrate-plus", "+", Action::Bitrate(1), false, cx))
            .child(self.button("recommended", "使用建議值", Action::Recommend, false, cx))
            .into_any_element()
    }
    pub fn settings(&self, narrow: bool, cx: &Context<Self>) -> AnyElement {
        let nav = div()
            .flex()
            .flex_col()
            .w(px(215.))
            .flex_shrink_0()
            .p_4()
            .gap_2()
            .bg(rgb(0x131b24))
            .border_r_1()
            .border_color(rgb(BORDER))
            .child(
                div()
                    .p_3()
                    .rounded(px(5.))
                    .bg(rgb(0x193139))
                    .text_color(rgb(ACCENT))
                    .child("串流"),
            )
            .children(
                [
                    "顯示",
                    "音效",
                    "主機",
                    "鍵盤與滑鼠",
                    "手把",
                    "相容性與診斷",
                    "介面",
                ]
                .into_iter()
                .map(|label| div().p_3().text_color(rgb(0x647384)).child(label)),
            )
            .child(
                div()
                    .mt_4()
                    .text_size(px(11.))
                    .text_color(rgb(MUTED))
                    .child("初版僅開放串流設定，其餘分類待後續移植。"),
            );
        let content = div()
            .flex_1()
            .min_w_0()
            .min_h_0()
            .flex()
            .flex_col()
            .child(
                div()
                    .id("settings-scroll")
                    .overflow_y_scroll()
                    .flex_1()
                    .min_h_0()
                    .p_7()
                    .child(div().text_size(px(27.)).mb_2().child("設定"))
                    .child(
                        div()
                            .text_size(px(12.))
                            .text_color(rgb(MUTED))
                            .mb_6()
                            .child("GPUI 串流設定初版 · 僅儲存範例偏好，不影響正式用戶端"),
                    )
                    .child(div().text_size(px(18.)).mb_2().child("畫面"))
                    .child(self.field(
                        "解析度",
                        "初版提供常用預設；自訂輸入待補。",
                        self.select(Field::Resolution, cx),
                        narrow,
                    ))
                    .child(self.field("影格率", "", self.select(Field::Fps, cx), narrow))
                    .child(self.field("視訊編碼", "", self.select(Field::Codec, cx), narrow))
                    .child(self.field(
                        if self.draft.abr {
                            "位元率上限"
                        } else {
                            "位元率"
                        },
                        "",
                        self.bitrate(cx),
                        narrow,
                    ))
                    .child(div().mt_6().text_size(px(18.)).child("連線穩定性"))
                    .child(self.field(
                        "自動調整位元率（ABR）",
                        "需要相容的 Sunshine 主機。",
                        self.toggle("abr", self.draft.abr, Action::Abr, cx),
                        narrow,
                    ))
                    .child(self.field(
                        "封包遺失保護（FEC）",
                        "與 ABR 可獨立使用；取決於主機支援。",
                        self.select(Field::Fec, cx),
                        narrow,
                    ))
                    .when(self.draft.fec == 2, |d| {
                        d.child(
                            self.field(
                                "固定 FEC 百分比",
                                "",
                                div()
                                    .flex()
                                    .items_center()
                                    .gap_3()
                                    .child(self.button(
                                        "fec-minus",
                                        "−",
                                        Action::FecPercent(-1),
                                        false,
                                        cx,
                                    ))
                                    .child(format!("{}%", self.draft.fec_percent))
                                    .child(self.button(
                                        "fec-plus",
                                        "+",
                                        Action::FecPercent(1),
                                        false,
                                        cx,
                                    ))
                                    .into_any_element(),
                                narrow,
                            ),
                        )
                    })
                    .child(div().mt_6().text_size(px(18.)).child("畫質增強"))
                    .child(self.field(
                        "AI 畫質增強",
                        "初版只預覽偏好；尚未偵測 GPU 支援。",
                        self.toggle("enhance", self.draft.enhancement, Action::Enhance, cx),
                        narrow,
                    ))
                    .child(self.field(
                        "串流渲染比例",
                        "",
                        self.toggle("scale", self.draft.scale, Action::Scale, cx),
                        narrow,
                    ))
                    .when(self.draft.scale, |d| {
                        d.child(
                            self.field(
                                "渲染比例",
                                "",
                                div()
                                    .flex()
                                    .items_center()
                                    .gap_3()
                                    .child(self.button(
                                        "scale-minus",
                                        "−",
                                        Action::ScalePercent(-5),
                                        false,
                                        cx,
                                    ))
                                    .child(format!("{}%", self.draft.scale_percent))
                                    .child(self.button(
                                        "scale-plus",
                                        "+",
                                        Action::ScalePercent(5),
                                        false,
                                        cx,
                                    ))
                                    .into_any_element(),
                                narrow,
                            ),
                        )
                    }),
            )
            .child(
                div()
                    .flex()
                    .when(narrow, |d| d.flex_col())
                    .items_center()
                    .justify_between()
                    .gap_3()
                    .p_4()
                    .bg(rgb(SURFACE))
                    .border_t_1()
                    .border_color(rgb(BORDER))
                    .child(
                        div()
                            .flex_1()
                            .text_size(px(12.))
                            .text_color(rgb(MUTED))
                            .child(if !self.notice.is_empty() {
                                self.notice.clone()
                            } else if self.draft != self.saved {
                                "有尚未儲存的變更".into()
                            } else {
                                "初版設定不會套用到真實串流".into()
                            }),
                    )
                    .child(
                        div()
                            .flex()
                            .gap_3()
                            .child(self.button(
                                "defaults",
                                "載入預設值",
                                Action::Defaults,
                                false,
                                cx,
                            ))
                            .child(self.button("save", "儲存設定", Action::Save, true, cx)),
                    ),
            );
        div()
            .flex()
            .flex_1()
            .min_h_0()
            .when(!narrow, |d| d.child(nav))
            .child(content)
            .into_any_element()
    }
    pub fn modal(&self, cx: &Context<Self>) -> AnyElement {
        let mut content = div().flex().flex_col().gap_4();
        let title;
        match self.modal.as_ref().unwrap() {
            Modal::Info(t, body) => {
                title = *t;
                content = content.child(*body);
            }
            Modal::Pair => {
                title = "配對主機（模擬）";
                content = content
                    .child("正式版需在 Sunshine 配對頁輸入 PIN；本初版不連線主機。")
                    .child(
                        div()
                            .text_size(px(40.))
                            .text_color(rgb(ACCENT))
                            .child("1234"),
                    )
                    .child(self.button("pair-success", "模擬配對成功", Action::Pair, true, cx));
            }
            Modal::Stream => {
                title = "串流流程預覽";
                content = content.child(
                    "正式版會切換至現有 SDL 串流視窗。此初版尚未接入影片、音效、輸入或剪貼簿傳送。",
                );
            }
            Modal::Select(field) => {
                let (t, options, current) = match field {
                    Field::Resolution => ("解析度", RESOLUTIONS, self.draft.resolution),
                    Field::Fps => ("影格率", FPS, self.draft.fps),
                    Field::Codec => ("視訊編碼", CODECS, self.draft.codec),
                    Field::Fec => ("封包遺失保護（FEC）", FEC, self.draft.fec),
                };
                title = t;
                let field = *field;
                content = content.children(options.iter().enumerate().map(|(i, option)| {
                    self.button(
                        ("option", i),
                        *option,
                        Action::Choose(field, i),
                        i == current,
                        cx,
                    )
                }));
            }
        }
        div()
            .absolute()
            .inset_0()
            .bg(rgba(0x050a12d9))
            .flex()
            .items_center()
            .justify_center()
            .p_5()
            .child(
                div()
                    .w(px(500.))
                    .max_w_full()
                    .p_6()
                    .rounded(px(8.))
                    .border_1()
                    .border_color(rgb(0x465565))
                    .bg(rgb(SURFACE))
                    .child(div().text_size(px(20.)).mb_5().child(title))
                    .child(content)
                    .child(div().mt_6().flex().justify_end().child(self.button(
                        "close",
                        "關閉",
                        Action::Close,
                        false,
                        cx,
                    ))),
            )
            .into_any_element()
    }
}
