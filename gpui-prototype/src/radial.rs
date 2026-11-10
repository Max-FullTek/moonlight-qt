use crate::*;

pub struct Menu {
    owner: Entity<Prototype>,
    controls: Controls,
    focus: FocusHandle,
    _subscription: Subscription,
}
impl Menu {
    pub fn new(owner: Entity<Prototype>, controls: Controls, window: &mut Window, cx: &mut Context<Self>) -> Self {
        let focus = cx.focus_handle();
        window.focus(&focus);
        let subscription = cx.observe(&owner, |this, owner, cx| {
            this.controls = owner.read(cx).controls.clone();
            cx.notify();
        });
        let weak = owner.downgrade();
        window.on_window_should_close(cx, move |window, cx| {
            let _ = weak.update(cx, |view, cx| view.navigate_controls("back", window, cx));
            false
        });
        Self { owner, controls, focus, _subscription: subscription }
    }
    fn navigate(&mut self, direction: &str, window: &mut Window, cx: &mut Context<Self>) {
        self.owner.update(cx, |view, cx| view.navigate_controls(direction, window, cx));
    }
}
pub fn label(action: ControlAction) -> &'static str {
    match action {
        ControlAction::Resume => "返回串流", ControlAction::GamepadMouse => "手把滑鼠",
        ControlAction::Disconnect => "暫時斷線", ControlAction::End => "結束工作階段",
        ControlAction::Stats => "效能統計", ControlAction::Fullscreen => "全螢幕",
        ControlAction::MouseMode => "滑鼠模式", ControlAction::Cursor => "顯示游標",
        ControlAction::Paste => "貼上文字", ControlAction::PointerLock => "游標範圍鎖定",
        ControlAction::BitrateDown => "降低位元率", ControlAction::BitrateUp => "提高位元率",
    }
}
fn icon(action: ControlAction) -> &'static str {
    match action {
        ControlAction::Resume => "▶", ControlAction::GamepadMouse => "gamepad",
        ControlAction::Disconnect => "Ⅱ", ControlAction::End => "⏻",
        ControlAction::Stats => "▥", ControlAction::Fullscreen => "⛶",
        ControlAction::MouseMode => "↖", ControlAction::Cursor => "➤",
        ControlAction::Paste => "▤", ControlAction::PointerLock => "⊡",
        ControlAction::BitrateDown => "−", ControlAction::BitrateUp => "+",
    }
}
impl Render for Menu {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let extent = window.viewport_size();
        let scale = (f32::from(extent.width) / 680.).min(f32::from(extent.height) / 680.).min(1.);
        let p = |n: f32| px(n * scale);
        let controls = &self.controls;
        let selected = controls.order[controls.selected];
        let editing = controls.editing;
        let center = if editing {
            if controls.moving.is_some() { "選擇目的位置\nEnter／A 交換圖示".to_owned() }
            else { "編輯圖示順序\n選擇圖示後按 Enter／A".to_owned() }
        } else if controls.confirm_end { "再次確認以結束工作階段\n主機將清理並還原顯示".to_owned() }
        else if selected == ControlAction::GamepadMouse {
            if !controls.has_gamepad { "未連接手把".to_owned() }
            else if controls.gamepad_mouse { "手把滑鼠：開啟".to_owned() }
            else { "手把滑鼠：關閉".to_owned() }
        } else if selected == ControlAction::Disconnect { "保留主機的 Desktop 工作階段".to_owned() }
        else { match selected {
            ControlAction::Resume => "關閉選單，回到串流操作",
            ControlAction::End => "通知主機結束工作階段並還原顯示",
            ControlAction::Stats => "顯示或隱藏串流效能統計",
            ControlAction::Fullscreen => "切換串流全螢幕顯示",
            _ => "",
        }.to_owned() };
        let items: Vec<_> = controls.order.iter().copied().enumerate().map(|(index, action)| {
            let (x, y) = controls::position(index);
            let highlighted = index == controls.selected;
            let picked = controls.moving == Some(index);
            div().id(("radial", index)).absolute().left(p(340. + x - 43.)).top(p(318. + y - 43.))
                .size(p(86.)).rounded_full().border_2()
                .border_color(rgb(if picked { 0xffcb70 } else if highlighted { ACCENT } else { BORDER }))
                .bg(rgba(if highlighted { 0x244e52f5 } else { 0x171f28ed }))
                .flex().flex_col().items_center().justify_center().gap_1().cursor_pointer()
                .child(if action == ControlAction::GamepadMouse {
                    svg().path("gamepad").size(p(34.)).text_color(rgb(TEXT)).into_any_element()
                } else { div().font_family("Segoe UI Symbol").text_size(p(34.)).child(icon(action)).into_any_element() })
                .on_hover(cx.listener(move |this, hovered: &bool, _, cx| {
                    if *hovered { this.owner.update(cx, |view, cx| {
                        if view.controls.selected != index {
                            view.controls.selected = index;
                            view.controls.confirm_end = false;
                            cx.notify();
                        }
                    }); }
                }))
                .on_click(cx.listener(move |this, _, window, cx| {
                    this.owner.update(cx, |view, cx| {
                        view.controls.selected = index;
                        view.navigate_controls("select", window, cx);
                    });
                }))
        }).collect();
        div().size_full().track_focus(&self.focus).text_color(rgb(TEXT))
            .on_key_down(cx.listener(|this, event: &KeyDownEvent, window, cx| {
                let key = &event.keystroke;
                let direction = match key.key.as_str() {
                    "up" => "up", "down" => "down", "left" => "left", "right" => "right",
                    "tab" if key.modifiers.shift => "previous", "tab" => "next",
                    "enter" | "space" => "select", "escape" => "back", "f2" => "edit",
                    "o" if key.modifiers.control && key.modifiers.alt && key.modifiers.shift => "back",
                    _ => return,
                };
                this.navigate(direction, window, cx);
                cx.stop_propagation();
            }))
            .child(div().relative().w(p(680.)).h(p(680.)).children(items)
                .child(div().absolute().left(p(230.)).top(p(208.)).size(p(220.))
                    .p_5().rounded_full().bg(rgba(0x10151df0)).flex().flex_col().items_center().justify_center().text_center().gap_3()
                    .child(div().text_size(p(20.)).child(label(selected)))
                    .child(div().text_size(p(13.)).child(center)))
                .child(div().absolute().top(p(610.)).w_full().flex().flex_col().items_center().gap_2()
                    .child(div().id("edit-order").cursor_pointer().text_color(rgb(ACCENT))
                        .child(if editing { "完成編輯（F2／X）" } else { "編輯圖示順序（F2／X）" })
                        .on_click(cx.listener(|this, _, window, cx| this.navigate("edit", window, cx))))
                    .child(div().text_size(p(12.)).child("方向鍵／十字鍵選擇 · Enter／A 確認 · Esc／B 返回"))))
    }
}
