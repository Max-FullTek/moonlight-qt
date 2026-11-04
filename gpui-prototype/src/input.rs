//! Address and setting entry. Windows WM_CHAR/Unicode input must use GPUI's
//! input handler; physical key-down events alone miss injected text and IMEs.
use crate::{Modal, Prototype};
use gpui::{Bounds, Context, EntityInputHandler, Pixels, Point, UTF16Selection, Window};
use std::ops::Range;

pub fn filter_text(text: &str, unicode: bool) -> String {
    let mut length = 0;
    text.chars()
        .filter(|c| {
            if unicode {
                !c.is_control()
            } else {
                c.is_ascii_graphic()
            }
        })
        .take_while(|c| {
            length += c.len_utf16();
            length <= 260
        })
        .collect()
}
fn byte_offset(text: &str, utf16: usize) -> Option<usize> {
    let mut offset = 0;
    for (byte, ch) in text.char_indices() {
        if offset == utf16 {
            return Some(byte);
        }
        offset += ch.len_utf16();
    }
    (offset == utf16).then_some(text.len())
}

impl EntityInputHandler for Prototype {
    fn text_for_range(
        &mut self,
        range: Range<usize>,
        actual: &mut Option<Range<usize>>,
        _: &mut Window,
        _: &mut Context<Self>,
    ) -> Option<String> {
        let bytes =
            byte_offset(&self.address, range.start)?..byte_offset(&self.address, range.end)?;
        *actual = Some(range.clone());
        self.address.get(bytes).map(str::to_owned)
    }
    fn selected_text_range(
        &mut self,
        _: bool,
        _: &mut Window,
        _: &mut Context<Self>,
    ) -> Option<UTF16Selection> {
        Some(UTF16Selection {
            range: self.address.encode_utf16().count()..self.address.encode_utf16().count(),
            reversed: false,
        })
    }
    fn marked_text_range(&self, _: &mut Window, _: &mut Context<Self>) -> Option<Range<usize>> {
        self.input_marked.clone()
    }
    fn unmark_text(&mut self, _: &mut Window, _: &mut Context<Self>) {
        self.input_marked = None;
    }
    fn replace_text_in_range(
        &mut self,
        range: Option<Range<usize>>,
        text: &str,
        _: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if !matches!(
            self.modal,
            Some(Modal::Add | Modal::EditValue(_) | Modal::RenameHost(_))
        ) {
            return;
        }
        let length = self.address.encode_utf16().count();
        let range = range
            .or_else(|| self.input_marked.take())
            .unwrap_or(length..length);
        let (Some(start), Some(end)) = (
            byte_offset(&self.address, range.start),
            byte_offset(&self.address, range.end),
        ) else {
            return;
        };
        if start > end {
            return;
        }
        let available = 260usize.saturating_sub(length - (range.end - range.start));
        let mut units = 0;
        let text: String = filter_text(text, self.unicode_input())
            .chars()
            .take_while(|c| {
                units += c.len_utf16();
                units <= available
            })
            .collect();
        self.address.replace_range(start..end, &text);
        self.input_marked = None;
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
        let start = range
            .as_ref()
            .map(|r| r.start)
            .or_else(|| self.input_marked.as_ref().map(|r| r.start))
            .unwrap_or_else(|| self.address.encode_utf16().count());
        self.replace_text_in_range(range, text, window, cx);
        let end = (start
            + filter_text(text, self.unicode_input())
                .encode_utf16()
                .count())
        .min(self.address.encode_utf16().count());
        self.input_marked = (start < end).then_some(start..end);
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
        Some(self.address.encode_utf16().count())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn unicode_offsets_never_split_a_surrogate_or_utf8_character() {
        let text = "電腦😀";
        assert_eq!(byte_offset(text, 1), Some(3));
        assert_eq!(byte_offset(text, 3), None);
        assert_eq!(byte_offset(text, 4), Some(text.len()));
        assert_eq!(filter_text("電腦 room😀\n", true), "電腦 room😀");
        assert_eq!(filter_text("主機 127.0.0.1\n", false), "127.0.0.1");
    }
}
