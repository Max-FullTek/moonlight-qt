use crate::*;
use settings::{CODECS, DECODERS, FEC, FPS, LANGUAGES, RESOLUTIONS, WINDOW_MODES};

impl Prototype {
    pub fn navigation(
        &self,
        id: &'static str,
        label: &'static str,
        icon: &'static str,
        action: Action,
        selected: bool,
        cx: &Context<Self>,
    ) -> Stateful<Div> {
        div()
            .id(id)
            .tab_index(0)
            .tab_stop(self.modal.is_none())
            .h_full()
            .px_4()
            .flex()
            .items_center()
            .gap_2()
            .cursor_pointer()
            .border_b_2()
            .border_color(rgb(if selected { ACCENT } else { BG }))
            .text_color(rgb(if selected { ACCENT } else { MUTED }))
            .focus(|style| style.bg(rgb(0x193139)))
            .hover(|style| style.bg(rgb(SURFACE)))
            .on_click(cx.listener(move |this, _, window, cx| {
                cx.stop_propagation();
                this.act(action.clone(), window, cx);
            }))
            .child(svg().path(icon).size(px(20.)).text_color(rgb(if selected {
                ACCENT
            } else {
                MUTED
            })))
            .child(self.tr(label))
    }
    pub fn button(
        &self,
        id: impl Into<ElementId>,
        label: impl Into<SharedString>,
        action: Action,
        primary: bool,
        cx: &Context<Self>,
    ) -> Stateful<Div> {
        let label: SharedString = label.into();
        div()
            .id(id)
            .tab_index(0)
            .tab_stop(
                self.modal.is_none()
                    || matches!(
                        action,
                        Action::Close
                            | Action::SubmitAddress
                            | Action::Stop
                            | Action::Control(_)
                            | Action::OpenControls
                            | Action::Choose(_, _)
                            | Action::AppPreferences(_, _, _)
                            | Action::SetAdvanced(_, _)
                            | Action::SubmitValue(_)
                            | Action::SubmitHostName(_)
                            | Action::RenameHost(_)
                            | Action::ConfirmRemove(_)
                            | Action::RemoveHost(_)
                            | Action::QuitHostApp
                    ),
            )
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
            .on_click(cx.listener(move |this, _, window, cx| {
                cx.stop_propagation();
                this.act(action.clone(), window, cx);
            }))
            .child(self.tr(&label))
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
                    .child(div().text_size(px(27.)).child(self.tr("裝置")))
                    .child(self.button("add", "+ 新增裝置", Action::Add, false, cx)),
            )
            .when(self.hosts.is_empty(), |d| {
                d.child(
                    div()
                        .mb_6()
                        .text_color(rgb(MUTED))
                        .child(self.tr("尚未新增裝置。按「新增裝置」輸入主機 IP。")),
                )
            })
            .child(div().flex().flex_wrap().justify_center().gap_7().children(
                self.hosts.iter().enumerate().map(|(i, host)| {
                    let status = if !host.online {
                        "離線"
                    } else if host.paired {
                        "已配對 · 在線"
                    } else {
                        "尚未配對 · 在線"
                    };
                    div()
                        .id(("device", i))
                        .tab_index(0)
                        .tab_stop(self.modal.is_none())
                        .cursor_pointer()
                        .w(px(240.))
                        .h(px(305.))
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
                                .text_color(rgb(if !host.online { MUTED } else { TEXT })),
                        )
                        .child(div().text_size(px(18.)).child(host.name.clone()))
                        .child(
                            div()
                                .text_size(px(12.))
                                .text_color(rgb(MUTED))
                                .child(self.tr(status)),
                        )
                        .child(
                            div()
                                .text_size(px(12.))
                                .text_color(rgb(MUTED))
                                .child(host.address.clone()),
                        )
                        .child(self.button(
                            ("host-apps", i),
                            if host.paired {
                                "開啟程式清單"
                            } else {
                                "開始 PIN 碼配對"
                            },
                            if host.paired {
                                Action::OpenApps(i)
                            } else {
                                Action::Device(i)
                            },
                            false,
                            cx,
                        ))
                        .child(self.button(
                            ("host-options", i),
                            "裝置選項",
                            Action::HostOptions(host.id.clone()),
                            false,
                            cx,
                        ))
                }),
            ))
            .into_any_element()
    }
    pub fn apps(&self, cx: &Context<Self>) -> AnyElement {
        let host = self
            .hosts
            .iter()
            .find(|host| Some(&host.id) == self.selected.as_ref());
        div()
            .flex_1()
            .min_h_0()
            .id("apps-scroll")
            .overflow_y_scroll()
            .p_8()
            .child(self.button("back", "‹ 返回裝置", Action::Devices, false, cx))
            .when(host.is_some_and(|h| h.current_app != 0), |d| {
                d.child(self.button(
                    "quit-host-app",
                    "結束主機程式",
                    Action::ConfirmQuit,
                    false,
                    cx,
                ))
            })
            .child(
                div()
                    .mt_6()
                    .mb_6()
                    .text_size(px(24.))
                    .child(host.map(|h| h.name.clone()).unwrap_or_default()),
            )
            .when(host.is_none_or(|h| h.apps.is_empty()), |d| {
                d.child(self.tr("正在取得程式清單；請確認 Sunshine 已設定桌面或遊戲。"))
            })
            .child(self.button(
                "show-hidden",
                if self.show_hidden {
                    "隱藏已隱藏項目"
                } else {
                    "顯示隱藏項目"
                },
                Action::ShowHidden,
                false,
                cx,
            ))
            .children(
                host.into_iter()
                    .flat_map(|h| h.apps.iter())
                    .filter(|app| self.show_hidden || !app.hidden)
                    .enumerate()
                    .map(|(i, app)| {
                        div()
                            .flex()
                            .items_center()
                            .gap_5()
                            .p_6()
                            .border_b_1()
                            .border_color(rgb(BORDER))
                            .child(svg().path("monitor").size(px(36.)).text_color(rgb(TEXT)))
                            .opacity(if app.hidden { 0.5 } else { 1.0 })
                            .child(
                                div()
                                    .flex_1()
                                    .text_size(px(18.))
                                    .child(app.name.clone())
                                    .when(app.direct_launch, |d| {
                                        d.child(
                                            div()
                                                .text_size(px(12.))
                                                .text_color(rgb(ACCENT))
                                                .child(self.tr("選取主機時自動啟動")),
                                        )
                                    }),
                            )
                            .child(self.button(
                                ("app-options", i),
                                "程式選項",
                                Action::AppOptions(app.id),
                                false,
                                cx,
                            ))
                            .child(self.button(
                                ("start", i),
                                if host.is_some_and(|h| h.current_app == app.id) {
                                    "恢復串流"
                                } else {
                                    "開始串流"
                                },
                                Action::Stream(app.id),
                                true,
                                cx,
                            ))
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
                    .child(self.tr(label))
                    .when(!hint.is_empty(), |d| {
                        d.child(
                            div()
                                .mt_1()
                                .text_size(px(12.))
                                .text_color(rgb(MUTED))
                                .child(self.tr(hint)),
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
            Field::WindowMode => ("window-mode", WINDOW_MODES, self.draft.window_mode),
            Field::Decoder => ("decoder", DECODERS, self.draft.decoder),
            Field::Language => ("language", LANGUAGES, self.draft.language),
        };
        self.button(
            id,
            format!(
                "{}     ▾",
                if field == Field::Language {
                    SharedString::from(options[current])
                } else {
                    self.tr(options[current])
                }
            ),
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
            .tab_stop(self.modal.is_none())
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
            .on_click(cx.listener(move |this, _, window, cx| {
                cx.stop_propagation();
                this.act(action.clone(), window, cx);
            }))
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
            .children(TABS.iter().enumerate().map(|(i, (tab, label))| {
                self.button(
                    ("settings-tab", i),
                    *label,
                    Action::SettingsTab(*tab),
                    self.settings_tab == *tab,
                    cx,
                )
            }));
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
                    .child(div().text_size(px(27.)).mb_2().child(self.tr("設定")))
                    .when(narrow, |d| {
                        d.child(div().flex().flex_wrap().gap_2().mb_4().children(
                            TABS.iter().enumerate().map(|(i, (tab, label))| {
                                self.button(
                                    ("narrow-tab", i),
                                    *label,
                                    Action::SettingsTab(*tab),
                                    self.settings_tab == *tab,
                                    cx,
                                )
                            }),
                        ))
                    })
                    .child(
                        div()
                            .text_size(px(12.))
                            .text_color(rgb(MUTED))
                            .mb_6()
                            .child(self.tr("GPUI 整合版 · 儲存後套用至下次串流")),
                    )
                    .when(self.settings_tab == SettingsTab::Display, |d| {
                        d.child(self.display_fields(narrow, cx))
                    })
                    .when(self.settings_tab == SettingsTab::Interface, |d| {
                        d.child(self.field(
                            "介面語言",
                            "保留繁體中文、简体中文與 English；不影響主機名稱或剪貼簿內容。",
                            self.select(Field::Language, cx),
                            narrow,
                        ))
                    })
                    .when(self.settings_tab == SettingsTab::Stream, |d| {
                        d.child(div().text_size(px(18.)).mb_2().child(self.tr("畫面")))
                            .child(self.field(
                                "解析度",
                                "可使用預設、自訂解析度或跟隨客戶端所在螢幕。",
                                self.select(Field::Resolution, cx),
                                narrow,
                            ))
                            .child(self.field("影格率", "", self.select(Field::Fps, cx), narrow))
                            .child(self.field(
                                "視訊編碼",
                                "",
                                self.select(Field::Codec, cx),
                                narrow,
                            ))
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
                            .child(div().mt_6().text_size(px(18.)).child(self.tr("連線穩定性")))
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
                            .child(div().mt_6().text_size(px(18.)).child(self.tr("畫質增強")))
                            .child(self.field(
                                "AI 畫質增強",
                                "由現有核心選擇解碼器；尚未在此頁偵測 GPU 支援。",
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
                            })
                    })
                    .child(self.advanced_fields(narrow, cx)),
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
                            .min_w_0()
                            .text_ellipsis()
                            .text_size(px(12.))
                            .text_color(rgb(MUTED))
                            .child(self.tr(if !self.notice.is_empty() {
                                &self.notice
                            } else if self.draft != self.saved {
                                "有尚未儲存的變更"
                            } else {
                                "下次串流使用已儲存的設定"
                            })),
                    )
                    .child(
                        div()
                            .flex()
                            .flex_shrink_0()
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
    fn display_fields(&self, narrow: bool, cx: &Context<Self>) -> AnyElement {
        div()
            .child(div().text_size(px(18.)).mb_2().child(self.tr("顯示模式")))
            .child(self.field(
                "啟動顯示模式",
                "串流中按 Ctrl + Alt + Shift + X 切換全螢幕／視窗。",
                self.select(Field::WindowMode, cx),
                narrow,
            ))
            .child(self.field(
                "HDR",
                "需要相容的主機、編碼器與 HDR 螢幕；由現有核心檢查支援。",
                self.toggle("hdr", self.draft.hdr, Action::Hdr, cx),
                narrow,
            ))
            .child(self.field(
                "垂直同步（V-Sync）",
                "減少畫面撕裂，可能增加延遲。",
                self.toggle("vsync", self.draft.vsync, Action::Vsync, cx),
                narrow,
            ))
            .child(self.field(
                "影格平順處理",
                "只在垂直同步開啟時生效。",
                self.toggle(
                    "frame-pacing",
                    self.draft.frame_pacing,
                    Action::FramePacing,
                    cx,
                ),
                narrow,
            ))
            .child(self.field(
                "視訊解碼器",
                "優先使用自動；強制硬體模式不支援時由核心回報錯誤。",
                self.select(Field::Decoder, cx),
                narrow,
            ))
            .into_any_element()
    }
    fn advanced_fields(&self, narrow: bool, cx: &Context<Self>) -> AnyElement {
        let mut fields = div();
        if self.settings_tab == SettingsTab::Interface {
            fields = fields.child(div().p_4().text_color(rgb(MUTED))
                .child(self.tr("串流進行中；Ctrl+Alt+Shift+O 開啟控制頁。")));
        }
        if self.settings_tab == SettingsTab::Host {
            fields = fields.child(self.button(
                "refresh-displays",
                "讀取已選主機的螢幕清單",
                Action::FetchDisplays,
                false,
                cx,
            ));
            for (i, display) in self.host_displays.iter().enumerate() {
                fields = fields.child(self.button(
                    ("host-display", i),
                    display.name.clone(),
                    Action::SetAdvanced("host_display", serde_json::json!(display.target)),
                    self.draft.option("host_display") == serde_json::json!(display.target),
                    cx,
                ));
            }
        }
        for spec in advanced::OPTIONS
            .iter()
            // Placement belongs to the retained Qt frontend. Keep its stored
            // value for profile compatibility; GPUI controls use a focused page.
            .filter(|s| s.category == self.settings_tab.category() && s.key != "overlay")
        {
            if (spec.key.starts_with("hdr_")
                && (!self.draft.hdr
                    || (matches!(spec.key, "hdr_peak" | "hdr_min" | "hdr_average")
                        && self.draft.option("hdr_brightness") != 2)))
                || (matches!(spec.key, "custom_width" | "custom_height")
                    && self.draft.resolution != 4)
                || (spec.key == "custom_fps" && self.draft.fps != 5)
            {
                continue;
            }
            let value = self.draft.option(spec.key);
            let control = match spec.kind {
                advanced::Kind::Toggle(_) => self.toggle(
                    spec.key,
                    value.as_bool().unwrap(),
                    Action::Advanced(spec.key),
                    cx,
                ),
                _ => {
                    let text = match spec.kind {
                        advanced::Kind::Choice(options, _) => {
                            self.tr(options[value.as_u64().unwrap() as usize])
                        }
                        advanced::Kind::Text => {
                            let v = value.as_str().unwrap();
                            SharedString::from(if v.is_empty() {
                                self.tr("跟隨主機").to_string()
                            } else {
                                v.to_owned()
                            })
                        }
                        _ => SharedString::from(value.to_string()),
                    };
                    self.button(spec.key, text, Action::Advanced(spec.key), false, cx)
                        .into_any_element()
                }
            };
            fields = fields.child(self.field(spec.label, spec.hint, control, narrow));
        }
        fields.into_any_element()
    }
    pub fn modal(&self, cx: &Context<Self>) -> AnyElement {
        let mut content = div().flex().flex_col().gap_4();
        let title;
        match self.modal.as_ref().unwrap() {
            Modal::HostOptions(host) => {
                title = "裝置選項";
                content = content
                    .child(self.button(
                        "rename-host",
                        "重新命名",
                        Action::RenameHost(host.clone()),
                        false,
                        cx,
                    ))
                    .child(self.button(
                        "remove-host",
                        "移除裝置",
                        Action::ConfirmRemove(host.clone()),
                        false,
                        cx,
                    ));
            }
            Modal::ConfirmRemove(host) => {
                title = "移除裝置";
                content = content
                    .child(self.tr("僅移除此客戶端的主機記錄；之後可重新新增並配對。"))
                    .child(self.button(
                        "confirm-remove",
                        "確認移除",
                        Action::RemoveHost(host.clone()),
                        true,
                        cx,
                    ));
            }
            Modal::ConfirmQuit => {
                title = "結束主機程式";
                content = content
                    .child(self.tr("未儲存的進度可能遺失。確定要結束主機上正在執行的程式？"))
                    .child(self.button(
                        "confirm-quit",
                        "確認結束程式",
                        Action::QuitHostApp,
                        true,
                        cx,
                    ));
            }
            Modal::Info(t, body) => {
                title = *t;
                content = content.child(self.tr(body));
            }
            Modal::Add | Modal::EditValue(_) | Modal::RenameHost(_) => {
                let entity = cx.entity();
                let focus = self.modal_focus.clone();
                let edit_key = match self.modal {
                    Some(Modal::EditValue(key)) => Some(key),
                    _ => None,
                };
                title = edit_key
                    .map(|k| advanced::spec(k).unwrap().label)
                    .unwrap_or(if matches!(self.modal, Some(Modal::RenameHost(_))) {
                        "重新命名"
                    } else {
                        "新增裝置"
                    });
                content = content
                    .child(self.tr(
                        if edit_key.is_some() || matches!(self.modal, Some(Modal::RenameHost(_))) {
                            "輸入新值；支援貼上，Ctrl+A 清空。"
                        } else {
                            "輸入 IP／主機名稱，可附連接埠。支援貼上；Ctrl+A 清空。"
                        },
                    ))
                    .child(
                        div()
                            .id("address-input")
                            .relative()
                            .tab_index(0)
                            .track_focus(&self.modal_focus)
                            .on_click(
                                cx.listener(|this, _, window, _| window.focus(&this.modal_focus)),
                            )
                            .p_4()
                            .border_1()
                            .border_color(rgb(ACCENT))
                            .bg(rgb(BG))
                            .child(format!("{}│", self.address))
                            .child(
                                canvas(
                                    |_, _, _| {},
                                    move |bounds, _, window, cx| {
                                        window.handle_input(
                                            &focus,
                                            ElementInputHandler::new(bounds, entity.clone()),
                                            cx,
                                        );
                                    },
                                )
                                .absolute()
                                .size_full(),
                            ),
                    )
                    .child(self.tr(&self.notice))
                    .child(self.button(
                        "submit-address",
                        if edit_key.is_some() || matches!(self.modal, Some(Modal::RenameHost(_))) {
                            "套用"
                        } else {
                            "新增主機"
                        },
                        if let Some(Modal::RenameHost(host)) = &self.modal {
                            Action::SubmitHostName(host.clone())
                        } else {
                            edit_key
                                .map(Action::SubmitValue)
                                .unwrap_or(Action::SubmitAddress)
                        },
                        true,
                        cx,
                    ));
            }
            Modal::Pair(pin) => {
                title = "配對主機";
                content = content
                    .child(self.tr("請在 Sunshine 的 PIN 配對頁輸入下列驗證碼，並等待配對完成。"))
                    .child(
                        div()
                            .text_size(px(40.))
                            .text_color(rgb(ACCENT))
                            .child(pin.clone()),
                    );
            }
            Modal::Stream => {
                title = "串流連線";
                content = content.child(self.tr(&self.stage)).child(self.button(
                    "stop-stream",
                      if self.streaming { "串流控制" } else { "結束連線" },
                      if self.streaming { Action::OpenControls } else { Action::Stop },
                    false,
                    cx,
                ));
            }
            Modal::Controls => {
                title = "串流控制";
                content = content
                    .child(self.tr("方向鍵／手把十字鍵選擇，Enter／A 確認，Esc／B 返回串流。"))
                    .child(self.tr("暫時斷線會保留 Desktop 工作階段；結束工作階段會請主機清理並依設定還原顯示。"))
                    .child(div().id("controls-list").overflow_y_scroll().max_h(px(340.))
                        .track_scroll(&self.controls_scroll).flex().flex_col().gap_2()
                        .children(ControlAction::ALL.iter().enumerate().map(|(index, &action)| {
                            let label = match action {
                                ControlAction::Resume => self.tr("返回串流"),
                                ControlAction::GamepadMouse => self.tr(if !self.controls.has_gamepad { "手把滑鼠：未連接手把" }
                                    else if self.controls.gamepad_mouse { "手把滑鼠：開啟（操作控制頁期間暫停）" } else { "手把滑鼠：關閉" }),
                                ControlAction::Disconnect => self.tr("暫時斷線"),
                                ControlAction::End => self.tr(if self.controls.confirm_end { "再次確認：結束工作階段" } else { "結束工作階段" }),
                                ControlAction::Stats => self.tr("效能統計開關"),
                                ControlAction::Fullscreen => self.tr("全螢幕開關"),
                                ControlAction::MouseMode => self.tr("滑鼠模式切換"),
                                ControlAction::Cursor => self.tr("游標顯示開關"),
                                ControlAction::Paste => self.tr("貼上文字"),
                                ControlAction::PointerLock => self.tr("游標範圍鎖定開關"),
                                ControlAction::BitrateDown | ControlAction::BitrateUp => format!("{}  {} Mbps", self.tr(if action == ControlAction::BitrateDown { "降低位元率" } else { "提高位元率" }), self.controls.bitrate).into(),
                            };
                            self.button(("control", index), label, Action::Control(action), self.controls.selected == index, cx)
                        })));
                if self.pending { content = content.child(self.tr("正在結束連線…")); }
            }
            Modal::AppOptions(id) => {
                title = "程式選項";
                if let Some(app) = self
                    .hosts
                    .iter()
                    .find(|h| Some(&h.id) == self.selected.as_ref())
                    .and_then(|h| h.apps.iter().find(|a| a.id == *id))
                {
                    content = content
                        .child(app.name.clone())
                        .child(self.tr("隱藏只影響此客戶端的清單；直啟會在選取主機時啟動此程式。"))
                        .child(self.button(
                            "app-hide",
                            if app.hidden {
                                "取消隱藏"
                            } else {
                                "隱藏此程式"
                            },
                            Action::AppPreferences(*id, !app.hidden, false),
                            false,
                            cx,
                        ))
                        .child(self.button(
                            "app-direct",
                            if app.direct_launch {
                                "取消直啟"
                            } else {
                                "設為直啟"
                            },
                            Action::AppPreferences(*id, false, !app.direct_launch),
                            false,
                            cx,
                        ));
                }
            }
            Modal::Advanced(key) => {
                let spec = advanced::spec(key).unwrap();
                title = spec.label;
                if let advanced::Kind::Choice(options, _) = spec.kind {
                    content = content.children(options.iter().enumerate().map(|(i, v)| {
                        self.button(
                            ("advanced-choice", i),
                            self.tr(v),
                            Action::SetAdvanced(key, serde_json::json!(i)),
                            self.draft.option(key) == serde_json::json!(i),
                            cx,
                        )
                    }));
                }
            }
            Modal::Select(field) => {
                let (t, options, current) = match field {
                    Field::Resolution => ("解析度", RESOLUTIONS, self.draft.resolution),
                    Field::Fps => ("影格率", FPS, self.draft.fps),
                    Field::Codec => ("視訊編碼", CODECS, self.draft.codec),
                    Field::Fec => ("封包遺失保護（FEC）", FEC, self.draft.fec),
                    Field::WindowMode => ("啟動顯示模式", WINDOW_MODES, self.draft.window_mode),
                    Field::Decoder => ("視訊解碼器", DECODERS, self.draft.decoder),
                    Field::Language => ("介面語言", LANGUAGES, self.draft.language),
                };
                title = t;
                let field = *field;
                content = content.children(options.iter().enumerate().map(|(i, option)| {
                    self.button(
                        ("option", i),
                        if field == Field::Language {
                            SharedString::from(*option)
                        } else {
                            self.tr(option)
                        },
                        Action::Choose(field, i),
                        i == current,
                        cx,
                    )
                }));
            }
        }
        div()
            .id("modal-overlay")
            .track_focus(&self.modal_focus)
            .tab_stop(false)
            .absolute()
            .inset_0()
            .occlude()
            .bg(rgba(0x050a12d9))
            .flex()
            .items_center()
            .justify_center()
            .p_5()
            .child(
                div()
                    .id("modal-scroll")
                    .overflow_y_scroll()
                    .max_h(relative(0.90))
                    .w(px(500.))
                    .max_w_full()
                    .p_6()
                    .rounded(px(8.))
                    .border_1()
                    .border_color(rgb(0x465565))
                    .bg(rgb(SURFACE))
                    .child(div().text_size(px(20.)).mb_5().child(self.tr(title)))
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
