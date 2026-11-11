#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    // An empty value also covers file clipboards and invalid or oversized
    // Unicode data. nullopt allows the caller to try other formats.
    std::optional<std::wstring> readText(size_t maxCharacters) const;
    std::vector<char> readPng(size_t maxBytes) const;

private:
    struct Window;
    std::unique_ptr<Window> m_Window;
};
