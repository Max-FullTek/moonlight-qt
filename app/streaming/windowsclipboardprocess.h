#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

// Owned by the Session thread and serviced from its existing SDL loop.
// The helper's stdin is overlapped so a hung helper cannot block streaming.
class WindowsClipboardProcess {
public:
    WindowsClipboardProcess();
    ~WindowsClipboardProcess();
    WindowsClipboardProcess(const WindowsClipboardProcess&) = delete;
    WindowsClipboardProcess& operator=(const WindowsClipboardProcess&) = delete;
    bool start(const std::wstring& executable);
    bool running() const;
    bool wait(unsigned timeoutMs) const;
    unsigned exitCode() const;
    bool write(std::string_view bytes);
    bool flush();
    size_t pendingBytes() const;
    std::string readOutput();
    std::string readErrors();
    void closeInput();
    void terminate();
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
