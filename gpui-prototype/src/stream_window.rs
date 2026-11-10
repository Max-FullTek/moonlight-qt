//! A focused GPUI menu owned by the existing SDL window. No frame transport,
//! global topmost flag, hooks, or background position polling.
use anyhow::{Result, ensure};
use raw_window_handle::{HasWindowHandle, RawWindowHandle};
use std::ffi::c_void;
type Hwnd = *mut c_void;
#[repr(C)]
#[derive(Default)]
struct Rect { left: i32, top: i32, right: i32, bottom: i32 }
#[link(name = "dwmapi")]
unsafe extern "system" {
    fn DwmSetWindowAttribute(window: Hwnd, attribute: u32, value: *const u32, size: u32) -> i32;
}
#[link(name = "user32")]
unsafe extern "system" {
    fn EnumWindows(callback: unsafe extern "system" fn(Hwnd, isize) -> i32, param: isize) -> i32;
    fn GetWindowThreadProcessId(window: Hwnd, pid: *mut u32) -> u32;
    fn GetClassNameW(window: Hwnd, name: *mut u16, capacity: i32) -> i32;
    fn IsWindowVisible(window: Hwnd) -> i32;
    fn IsIconic(window: Hwnd) -> i32;
    fn GetWindowRect(window: Hwnd, rect: *mut Rect) -> i32;
    fn GetDpiForWindow(window: Hwnd) -> u32;
    fn SetWindowLongPtrW(window: Hwnd, index: i32, value: isize) -> isize;
    fn GetWindowLongPtrW(window: Hwnd, index: i32) -> isize;
    fn SetWindowPos(window: Hwnd, after: Hwnd, x: i32, y: i32, w: i32, h: i32, flags: u32) -> i32;
}
struct Search { pid: u32, found: Hwnd }
unsafe extern "system" fn find(window: Hwnd, param: isize) -> i32 {
    let search = unsafe { &mut *(param as *mut Search) };
    let mut pid = 0;
    let mut class = [0u16; 64];
    unsafe {
        GetWindowThreadProcessId(window, &mut pid);
        let len = GetClassNameW(window, class.as_mut_ptr(), class.len() as i32);
        if pid == search.pid && IsWindowVisible(window) != 0
            && String::from_utf16_lossy(&class[..len.max(0) as usize]) == "SDL_app" {
            search.found = window;
            return 0;
        }
    }
    1
}
pub fn attach(window: &gpui::Window, pid: u32) -> Result<()> {
    let mut search = Search { pid, found: std::ptr::null_mut() };
    unsafe { EnumWindows(find, &mut search as *mut Search as isize); }
    ensure!(!search.found.is_null(), "找不到串流視窗");
    let raw = HasWindowHandle::window_handle(window).map_err(|e| anyhow::anyhow!("{e:?}"))?;
    let RawWindowHandle::Win32(handle) = raw.as_raw() else { anyhow::bail!("不是 Windows 視窗") };
    let menu = handle.hwnd.get() as Hwnd;
    let mut rect = Rect::default();
    unsafe {
        ensure!(IsIconic(search.found) == 0, "串流視窗已最小化");
        ensure!(GetWindowRect(search.found, &mut rect) != 0, "無法讀取串流視窗範圍");
        // GWLP_HWNDPARENT sets the owner for a top-level popup, not a child.
        SetWindowLongPtrW(menu, -8, search.found as isize);
        ensure!(GetWindowLongPtrW(menu, -8) == search.found as isize, "無法設定串流選單 owner");
        // GPUI 0.2.2's PopUp backend uses style 0; express the native popup
        // relationship explicitly while retaining its tool-window style.
        let style = GetWindowLongPtrW(menu, -16);
        SetWindowLongPtrW(menu, -16, style | 0x80000000);
        // The transparent menu has its own circular surfaces. Suppress the
        // system's rectangular frame/shadow; older Windows may ignore colors.
        DwmSetWindowAttribute(menu, 2, &2, 4); // DWMNCRP_DISABLED
        DwmSetWindowAttribute(menu, 33, &1, 4); // DWMWCP_DONOTROUND
        DwmSetWindowAttribute(menu, 34, &0xfffffffe, 4); // DWMWA_COLOR_NONE
        let dpi = GetDpiForWindow(search.found).max(96) as f32 / 96.;
        let edge = ((680. * dpi).round() as i32).min(rect.right - rect.left).min(rect.bottom - rect.top);
        ensure!(SetWindowPos(menu, std::ptr::null_mut(),
            (rect.left + rect.right - edge) / 2, (rect.top + rect.bottom - edge) / 2,
            edge, edge, 0x0070) != 0, "無法定位串流選單"); // NOACTIVATE | SHOWWINDOW | FRAMECHANGED
    }
    Ok(())
}
