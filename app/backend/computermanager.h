#pragma once

#include "nvcomputer.h"
#include "settings/streamingpreferences.h"
#include "settings/compatfetcher.h"

#ifndef LITE_GPUI_WORKER
#include <qmdnsengine/server.h>
#include <qmdnsengine/cache.h>
#include <qmdnsengine/browser.h>
#include <qmdnsengine/service.h>
#include <qmdnsengine/resolver.h>
#endif

#include <QVariant>
#ifdef LITE_GPUI_WORKER
#include "windowstaskqueue.h"
#endif
#include <QTimer>
#include <QSet>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>

class ComputerManager;

#ifndef LITE_GPUI_WORKER
class MdnsPendingComputer : public QObject
{
    Q_OBJECT

public:
    explicit MdnsPendingComputer(const QSharedPointer<QMdnsEngine::Server> server,
                                 const QMdnsEngine::Service& service)
        : m_Hostname(service.hostname()),
          m_Port(service.port()),
          m_ServerWeak(server),
          m_Resolver(nullptr)
    {
        // Start resolving
        resolve();
    }

    virtual ~MdnsPendingComputer()
    {
        delete m_Resolver;
    }

    QString hostname()
    {
        return m_Hostname;
    }

    uint16_t port()
    {
        return m_Port;
    }

private slots:
    void handleResolvedTimeout()
    {
        if (m_Addresses.isEmpty()) {
            if (m_Retries-- > 0) {
                // Try again
                qInfo() << "Resolving" << hostname() << "timed out. Retrying...";
                resolve();
            }
            else {
                qWarning() << "Giving up on resolving" << hostname() << "after repeated failures";
                cleanup();
            }
        }
        else {
            Q_ASSERT(!m_Addresses.isEmpty());
            emit resolvedHost(this, m_Addresses);
        }
    }

    void handleResolvedAddress(const QHostAddress& address)
    {
        m_Addresses.push_back(address);
    }

signals:
    void resolvedHost(MdnsPendingComputer*,QVector<QHostAddress>&);

private:
    void cleanup()
    {
        // Delete our resolver, so we're guaranteed that nothing is referencing m_Server.
        delete m_Resolver;
        m_Resolver = nullptr;

        // Now delete our strong reference that we held on behalf of m_Resolver.
        // The server may be destroyed after we make this call.
        m_Server.reset();
    }

    void resolve()
    {
        // Clean up any existing resolver object and server references
        cleanup();

        // Re-acquire a strong reference if the server still exists.
        m_Server = m_ServerWeak.toStrongRef();
        if (!m_Server) {
            return;
        }

        m_Resolver = new QMdnsEngine::Resolver(m_Server.data(), m_Hostname);
        connect(m_Resolver, &QMdnsEngine::Resolver::resolved,
                this, &MdnsPendingComputer::handleResolvedAddress);
        QTimer::singleShot(2000, this, &MdnsPendingComputer::handleResolvedTimeout);
    }

    QByteArray m_Hostname;
    uint16_t m_Port;
    QWeakPointer<QMdnsEngine::Server> m_ServerWeak;
    QSharedPointer<QMdnsEngine::Server> m_Server;
    QMdnsEngine::Resolver* m_Resolver;
    QVector<QHostAddress> m_Addresses;
    int m_Retries = 10;
};
#endif

class ComputerPollingEntry;

class ComputerManager : public QObject
{
    Q_OBJECT

    friend class GpuiCoreBridge;
    friend class DeferredHostDeletionTask;
    friend class PendingAddTask;
    friend class PendingPairingTask;
    friend class PendingQuitTask;

public:
    explicit ComputerManager(StreamingPreferences* prefs, bool externalPolling = false,
                             QString profileDirectory = {});

    virtual ~ComputerManager();

    Q_INVOKABLE void startPolling();

    Q_INVOKABLE void stopPollingAsync();

    Q_INVOKABLE void addNewHostManually(QString address);

    void addNewHost(NvAddress address, bool mdns, QString name = QString(), NvAddress mdnsIpv6Address = NvAddress());

    QString generatePinString();

    void pairHost(NvComputer* computer, QString pin);

    void quitRunningApp(NvComputer* computer);

    QVector<NvComputer*> getComputers();

    // computer is deleted inside this call
    void deleteHost(NvComputer* computer);

    void renameHost(NvComputer* computer, QString name);

    void clientSideAttributeUpdated(NvComputer* computer);

    // Install before starting host tasks. GPUI posts operation results to its
    // native owner; the Qt client retains its queued completion delivery.
    void setHostCompletionDispatcher(std::function<void(std::function<void()>)> dispatcher)
    {
        m_HostCompletionDispatcher = std::move(dispatcher);
    }
    void interruptPolling();
    void beginShutdown();

signals:
    void computerStateChanged(NvComputer* computer);

    void pairingCompleted(NvComputer* computer, QString error);

    void computerAddCompleted(QVariant success, QVariant detectedPortBlocking);

    void quitAppCompleted(QVariant error);
    void hostDeleted(QString uuid);

private slots:
    void handleAboutToQuit();

    void handleComputerStateChanged(NvComputer* computer);

#ifndef LITE_GPUI_WORKER
    void handleMdnsServiceResolved(MdnsPendingComputer* computer, QVector<QHostAddress>& addresses);
#endif

private:
    void saveHosts();
    void finishHostDeletion(NvComputer* computer, const QString& uuid);
    void submitHostTask(std::function<void()> task);
    void dispatchHostCompletion(std::function<void()> completion);
    bool waitBeforeAddRetry();

    void runDelayedFlush();
#ifndef LITE_GPUI_WORKER
    void loadLegacyHosts();
#endif

    void saveHost(NvComputer* computer);

#ifndef LITE_GPUI_WORKER
    QHostAddress getBestGlobalAddressV6(QVector<QHostAddress>& addresses);
#endif

    void startPollingComputer(NvComputer* computer);

    StreamingPreferences* m_Prefs;
#ifdef LITE_GPUI_WORKER
    WindowsTaskQueue m_HostTasks;
#endif
    std::function<void(std::function<void()>)> m_HostCompletionDispatcher;
    std::atomic_bool m_Stopping{false};
    std::mutex m_AddRetryMutex;
    std::condition_variable m_AddRetryCondition;
    bool m_ExternalPolling;
    const QString m_ProfileDirectory;
    int m_PollingRef;
    std::shared_mutex m_Lock;
    QMap<QString, NvComputer*> m_KnownHosts;
    QSet<NvComputer*> m_DeletingHosts; // Main-thread tombstones for queued polling events
    QMap<QString, ComputerPollingEntry*> m_PollEntries;
    QHash<QString, NvComputer> m_LastSerializedHosts; // Protected by m_DelayedFlushMutex
#ifndef LITE_GPUI_WORKER
    QSharedPointer<QMdnsEngine::Server> m_MdnsServer;
    QMdnsEngine::Browser* m_MdnsBrowser;
    QVector<MdnsPendingComputer*> m_PendingResolution;
#endif
    CompatFetcher m_CompatFetcher;
    std::thread m_DelayedFlushThread;
    std::mutex m_DelayedFlushMutex; // Lock ordering: Must never be acquired while holding NvComputer lock
    std::condition_variable m_DelayedFlushCondition;
    bool m_NeedsDelayedFlush;
    bool m_DelayedFlushStopping = false; // Protected by m_DelayedFlushMutex.
};
