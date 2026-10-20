//! Minimal ASCII address entry. Windows WM_CHAR/Unicode input must use GPUI's
//! input handler; physical key-down events alone miss injected text and IMEs.
use crate::*;
use std::ops::Range;

impl EntityInputHandler for Prototype {
    fn text_for_range(
        &mut self,
        range: Range<usize>,
        actual: &mut Option<Range<usize>>,
        _: &mut Window,
        _: &mut Context<Self>,
    ) -> Option<String> {
        let range = range.start.min(self.address.len())..range.end.min(self.address.len());
        *actual = Some(range.clone());
        self.address.get(range).map(str::to_owned)
    }
    fn selected_text_range(
        &mut self,
        _: bool,
        _: &mut Window,
        _: &mut Context<Self>,
    ) -> Option<UTF16Selection> {
        Some(UTF16Selection {
            range: self.address.len()..self.address.len(),
            reversed: false,
        })
    }
    fn marked_text_range(&self, _: &mut Window, _: &mut Context<Self>) -> Option<Range<usize>> {
        None
    }
    fn unmark_text(&mut self, _: &mut Window, _: &mut Context<Self>) {}
    fn replace_text_in_range(
        &mut self,
        range: Option<Range<usize>>,
        text: &str,
        _: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if !matches!(self.modal, Some(Modal::Add)) {
            return;
        }
        let range = range.unwrap_or(self.address.len()..self.address.len());
        if range.start > range.end || range.end > self.address.len() {
            return;
        }
        let available = 260 - (self.address.len() - range.len());
        let text: String = text
            .chars()
            .filter(|c| c.is_ascii_graphic())
            .take(available)
            .collect();
        self.address.replace_range(range, &text);
        cx.notify();
    }
    fn replace_and_mark_text_in_range(
        &mut self,
        range: Option<Range<usize>>,
        text: &str,
        _: Option<Range<usize>>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        self.replace_text_in_range(range, text, window, cx);
    }
    fn bounds_for_range(
        &mut self,
        _: Range<usize>,
        bounds: Bounds<Pixels>,
        _: &mut Window,
        _: &mut Context<Self>,
    ) -> Option<Bounds<Pixels>> {
        Some(bounds)
    }
    fn character_index_for_point(
        &mut self,
        _: Point<Pixels>,
        _: &mut Window,
        _: &mut Context<Self>,
    ) -> Option<usize> {
        Some(self.address.len())
    }
}
