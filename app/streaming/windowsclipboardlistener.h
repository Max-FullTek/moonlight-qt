#pragma once

#include <functional>
#include <memory>
#include <string_view>

// Create and destroy on the thread that dispatches Win32 window messages.
class WindowsClipboardListener {
public:
    explicit WindowsClipboardListener(std::function<void()> changed);
    ~WindowsClipboardListener();
    WindowsClipboardListener(const WindowsClipboardListener&) = delete;
    WindowsClipboardListener& operator=(const WindowsClipboardListener&) = delete;

    bool active() const;
    enum class TextWriteResult { Written, FileClipboard, Unavailable };
    TextWriteResult writeText(std::wstring_view text) const;

private:
    struct Window;
    std::unique_ptr<Window> m_Window;
};
