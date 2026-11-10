#include "qtnativehttptransport.h"

#include <QCoreApplication>
#include <QAbstractEventDispatcher>
#include <QEventLoop>
#include <QSocketNotifier>
#include <QTimer>

namespace NativeHttp {
namespace {
class QtOwnerLoop final : public EventLoop {
public:
    QtOwnerLoop()
    {
        m_Timer.setSingleShot(true);
        m_Timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_Timer, &QTimer::timeout, &m_Loop, [&] {
            wake({ReadyEvent::Kind::Timeout});
        });
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &m_Loop, [&] {
            m_Closing = true;
            wake({ReadyEvent::Kind::Cancel});
        });
    }
    ReadyEvent wait(const WaitState& state) override
    {
        if (m_Closing) return {};
        m_Ready = {};
        m_Woken = false;
        std::vector<std::unique_ptr<QSocketNotifier>> notifiers;
        for (const auto& socket : state.sockets) {
            auto watch = [&](QSocketNotifier::Type type) {
                auto notifier = std::make_unique<QSocketNotifier>(static_cast<qintptr>(socket.socket), type);
                QObject::connect(notifier.get(), &QSocketNotifier::activated, &m_Loop,
                                 [&, descriptor = socket.socket, type](QSocketDescriptor, QSocketNotifier::Type) {
                    wake({ReadyEvent::Kind::Socket, descriptor, type == QSocketNotifier::Read,
                          type == QSocketNotifier::Write, type == QSocketNotifier::Exception});
                });
                notifiers.push_back(std::move(notifier));
            };
            if (socket.read) watch(QSocketNotifier::Read);
            if (socket.write) watch(QSocketNotifier::Write);
            watch(QSocketNotifier::Exception);
        }
        if (state.timeoutMs >= 0) m_Timer.start(static_cast<int>(state.timeoutMs));
        m_Loop.exec(QEventLoop::ExcludeUserInputEvents);
        m_Timer.stop();
        // Destroy all watchers before libcurl can replace or close its sockets.
        // An application shutdown can also exit exec() without a socket wake.
        return m_Ready;
    }
private:
    void wake(ReadyEvent ready)
    {
        if (m_Woken) return;
        m_Woken = true;
        m_Ready = ready;
        m_Loop.quit();
    }
    QEventLoop m_Loop;
    QTimer m_Timer;
    ReadyEvent m_Ready;
    bool m_Woken = false;
    bool m_Closing = false;
};
}
Response performWithQtEvents(const Request& request)
{
    if (!QCoreApplication::instance()) {
        Response response;
        response.error = Error::InvalidRequest;
        response.message = "HTTP owner requires an existing event dispatcher";
        return response;
    }
    QtOwnerLoop loop;
    // QEventLoop creates the current worker's dispatcher when needed, just as
    // the existing NvHTTP nested loop does. Do not reject that valid owner.
    if (!QAbstractEventDispatcher::instance()) {
        Response response;
        response.error = Error::InvalidRequest;
        response.message = "HTTP owner requires an existing event dispatcher";
        return response;
    }
    return perform(request, loop);
}
}
