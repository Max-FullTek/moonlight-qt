#pragma once

#include "backend/nativehttptransport.h"
#include <QObject>
#include <QTimer>
#include <functional>

class QSocketNotifier;

// Transitional event adapter for the existing clipboard helper GUI thread.
class QtClipboardTransfer final : public QObject {
public:
    using Completion = std::function<void(QtClipboardTransfer*, NativeHttp::Response)>;
    QtClipboardTransfer(NativeHttp::Request request, Completion completed, QObject* parent);
    ~QtClipboardTransfer() override;
    void cancel();

private:
    void advance(NativeHttp::ReadyEvent event);
    void clearWatchers();
    std::unique_ptr<NativeHttp::AsyncRequest> m_Request;
    Completion m_Completed;
    QTimer m_Timer;
    std::vector<QSocketNotifier*> m_Watchers;
};
