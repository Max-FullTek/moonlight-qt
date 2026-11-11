#pragma once

#include "nativehttptransport.h"
#include <functional>

namespace NativeHttp {
// Single owner thread; the owner dispatches Win32 messages. No polling or
// threads. The initial request and completion are deferred through that queue.
class WindowsTransfer {
public:
    using Completion = std::function<void(Response)>;
    WindowsTransfer(Request request, Completion completed);
    ~WindowsTransfer();
    WindowsTransfer(const WindowsTransfer&) = delete;
    WindowsTransfer& operator=(const WindowsTransfer&) = delete;
    bool active() const;
    // Completes synchronously with Cancelled. Destruction cancels silently.
    void cancel();
private:
    struct Window;
    std::unique_ptr<Window> m_Window;
};
}
