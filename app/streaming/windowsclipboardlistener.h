#pragma once

#include <functional>
#include <memory>

// Create and destroy on the thread that dispatches Win32 window messages.
class WindowsClipboardListener {
public:
    explicit WindowsClipboardListener(std::function<void()> changed);
    ~WindowsClipboardListener();
    WindowsClipboardListener(const WindowsClipboardListener&) = delete;
    WindowsClipboardListener& operator=(const WindowsClipboardListener&) = delete;

    bool active() const;

private:
    struct Window;
    std::unique_ptr<Window> m_Window;
};
