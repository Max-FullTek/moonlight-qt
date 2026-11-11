#include "qtclipboardtransfer.h"
#include <QSocketNotifier>
#include <utility>

using NativeHttp::ReadyEvent;

QtClipboardTransfer::QtClipboardTransfer(NativeHttp::Request request, Completion completed, QObject* parent)
    : QObject(parent),
      m_Request(std::make_unique<NativeHttp::AsyncRequest>(std::move(request))),
      m_Completed(std::move(completed))
{
    m_Timer.setSingleShot(true);
    m_Timer.setTimerType(Qt::PreciseTimer);
    connect(&m_Timer, &QTimer::timeout, this, [this] { advance({ReadyEvent::Kind::Timeout}); });
    m_Timer.start(0);
}

QtClipboardTransfer::~QtClipboardTransfer()
{
    clearWatchers();
}

void QtClipboardTransfer::clearWatchers()
{
    m_Timer.stop();
    for (auto* watcher : m_Watchers) {
        watcher->setEnabled(false);
        watcher->deleteLater();
    }
    m_Watchers.clear();
}

void QtClipboardTransfer::cancel()
{
    clearWatchers();
    m_Request.reset();
    deleteLater();
}

void QtClipboardTransfer::advance(ReadyEvent event)
{
    if (!m_Request) return;
    clearWatchers();
    if (auto response = m_Request->advance(event)) {
        m_Request.reset();
        deleteLater();
        auto completed = std::move(m_Completed);
        completed(this, std::move(*response));
        return;
    }

    const auto state = m_Request->waitState();
    for (const auto& socket : state.sockets) {
        auto watch = [&](QSocketNotifier::Type type) {
            auto* notifier = new QSocketNotifier(static_cast<qintptr>(socket.socket), type, this);
            connect(notifier, &QSocketNotifier::activated, this, [this, descriptor = socket.socket, type] {
                advance({ReadyEvent::Kind::Socket, descriptor, type == QSocketNotifier::Read,
                         type == QSocketNotifier::Write, type == QSocketNotifier::Exception});
            });
            m_Watchers.push_back(notifier);
        };
        if (socket.read) watch(QSocketNotifier::Read);
        if (socket.write) watch(QSocketNotifier::Write);
        watch(QSocketNotifier::Exception);
    }
    if (state.timeoutMs >= 0) m_Timer.start(static_cast<int>(state.timeoutMs));
}
