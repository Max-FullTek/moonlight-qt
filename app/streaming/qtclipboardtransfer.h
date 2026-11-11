#pragma once

#include "backend/nativehttptransport.h"
#include <QObject>
#include <functional>

namespace NativeHttp { class WindowsTransfer; }

// Transitional lifetime adapter for the existing QObject clipboard owner.
// Socket/timer dispatch is native and does not depend on the Qt event loop.
class QtClipboardTransfer final : public QObject {
public:
    using Completion = std::function<void(QtClipboardTransfer*, NativeHttp::Response)>;
    QtClipboardTransfer(NativeHttp::Request request, Completion completed, QObject* parent);
    ~QtClipboardTransfer() override;
    void cancel();

private:
    void finish(NativeHttp::Response response);
    Completion m_Completed;
    std::unique_ptr<NativeHttp::WindowsTransfer> m_Transfer;
};
