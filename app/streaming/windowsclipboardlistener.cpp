#include "windowsclipboardlistener.h"

#include <windows.h>
#include <utility>

namespace {
struct ClipboardLock {
    ~ClipboardLock() { CloseClipboard(); }
};

struct GlobalMemoryLock {
    HGLOBAL buffer;
    void* data = GlobalLock(buffer);
    ~GlobalMemoryLock() { if (data) GlobalUnlock(buffer); }
};

bool hasFileFormats()
{
    if (IsClipboardFormatAvailable(CF_HDROP)) return true;
    for (const auto* name : {L"FileNameW", L"FileGroupDescriptor", L"FileGroupDescriptorW",
                             L"FileContents", L"Shell IDList Array"}) {
        const auto format = RegisterClipboardFormatW(name);
        if (format && IsClipboardFormatAvailable(format)) return true;
    }
    return false;
}

std::wstring decodeClipboardText(std::wstring_view buffer, size_t maxCharacters)
{
    std::wstring text;
    for (size_t index = 0; index < buffer.size(); ++index) {
        const wchar_t character = buffer[index];
        if (character == 0) return text;
        if (character == L'\r' && index + 1 < buffer.size() && buffer[index + 1] == L'\n') continue;
        if (text.size() == maxCharacters) return {};
        text.push_back(character);
    }
    // A provider must terminate CF_UNICODETEXT within its allocation.
    return {};
}
}

struct WindowsClipboardListener::Window {
    static constexpr wchar_t ClassName[] = L"MoonlightLiteClipboardListener";

    explicit Window(std::function<void()> callback) : changed(std::move(callback))
    {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = dispatch;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = ClassName;
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

        handle = CreateWindowExW(0, ClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        if (handle && !AddClipboardFormatListener(handle)) {
            DestroyWindow(handle);
            handle = nullptr;
        }
    }

    ~Window()
    {
        if (handle) {
            RemoveClipboardFormatListener(handle);
            DestroyWindow(handle);
        }
    }

    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        } else if (message == WM_CLIPBOARDUPDATE) {
            auto* listener = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (listener) listener->changed();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    std::function<void()> changed;
    HWND handle = nullptr;
};

WindowsClipboardListener::WindowsClipboardListener(std::function<void()> changed)
    : m_Window(std::make_unique<Window>(std::move(changed)))
{}

WindowsClipboardListener::~WindowsClipboardListener() = default;

bool WindowsClipboardListener::active() const
{
    return m_Window->handle != nullptr;
}

WindowsClipboardListener::TextWriteResult WindowsClipboardListener::writeText(std::wstring_view text) const
{
    if (!active() || !OpenClipboard(m_Window->handle)) return TextWriteResult::Unavailable;
    const ClipboardLock lock;

    // Check again with the clipboard open, so a concurrent file copy cannot
    // be overwritten between the format check and EmptyClipboard.
    if (hasFileFormats()) return TextWriteResult::FileClipboard;

    const auto buffer = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (text.size() * 2 + 1) * sizeof(wchar_t));
    if (!buffer) return TextWriteResult::Unavailable;
    auto* output = static_cast<wchar_t*>(GlobalLock(buffer));
    if (!output) {
        GlobalFree(buffer);
        return TextWriteResult::Unavailable;
    }
    wchar_t previous = 0;
    for (const wchar_t character : text) {
        if (character == L'\n' && previous != L'\r') *output++ = L'\r';
        *output++ = character;
        previous = character;
    }
    *output = 0;
    GlobalUnlock(buffer);
    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, buffer)) return TextWriteResult::Written;
    GlobalFree(buffer);
    return TextWriteResult::Unavailable;
}

std::optional<std::wstring> WindowsClipboardListener::readText(size_t maxCharacters) const
{
    if (!active() || !OpenClipboard(m_Window->handle)) return std::nullopt;
    const ClipboardLock lock;
    if (hasFileFormats()) return std::wstring();

    const auto buffer = GetClipboardData(CF_UNICODETEXT);
    if (!buffer) return std::nullopt;
    const GlobalMemoryLock memory{buffer};
    const auto* characters = static_cast<const wchar_t*>(memory.data);
    if (!characters) return std::nullopt;
    return decodeClipboardText({characters, GlobalSize(buffer) / sizeof(wchar_t)}, maxCharacters);
}

std::vector<char> WindowsClipboardListener::readPng(size_t maxBytes) const
{
    if (!active() || !OpenClipboard(m_Window->handle)) return {};
    const ClipboardLock lock;

    for (const auto* name : {L"image/png", L"PNG"}) {
        const auto format = RegisterClipboardFormatW(name);
        if (!format) continue;
        const auto buffer = GetClipboardData(format);
        if (!buffer) continue;
        const size_t size = GlobalSize(buffer);
        if (!size || size > maxBytes) continue;
        const GlobalMemoryLock memory{buffer};
        const auto* bytes = static_cast<const char*>(memory.data);
        if (!bytes) continue;
        // Some providers use a registered PNG name for other image data.
        constexpr std::string_view signature("\x89PNG\r\n\x1a\n", 8);
        if (size >= signature.size() && std::string_view(bytes, signature.size()) == signature) {
            return {bytes, bytes + size};
        }
    }
    return {};
}
