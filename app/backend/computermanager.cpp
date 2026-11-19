#include "computermanager.h"
#include "nvcomputer.h"
#include "nvhttp.h"
#include "nvpairingmanager.h"
#include "../path.h"
#ifdef LITE_GPUI_WORKER
#include "windowsreachability.h"
#endif
#ifdef LITE_NATIVE_WORKER_JSON
#include "hostprofile.h"
#endif

#include <Limelight.h>
#include <QtEndian>

#ifdef LITE_GPUI_WORKER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <openssl/rand.h>
#else
#include <QRandomGenerator>
#include <QThread>
#include <QThreadPool>
#endif
#include <QCoreApplication>
#include <QDir>
#include <chrono>
#include <cstdint>
#include <memory>

#ifndef LITE_GPUI_WORKER
#include <QSettings>
#define SER_HOSTS "hosts"
#define SER_HOSTS_BACKUP "hostsbackup"
#endif

class PcMonitorThread
#ifndef LITE_GPUI_WORKER
    : public QThread
#endif
{
#define TRIES_BEFORE_OFFLINING 2
#define POLLS_PER_APPLIST_FETCH 10

public:
    PcMonitorThread(NvComputer* computer, std::function<void(NvComputer*)> notify)
        : m_Computer(computer), m_Notify(std::move(notify))
    {
#ifndef LITE_GPUI_WORKER
        setObjectName("Polling thread for " + computer->name);
#endif
    }

#ifdef LITE_GPUI_WORKER
    void start()
    {
        m_Thread = std::thread([this] {
            SetThreadDescription(GetCurrentThread(), L"Moonlight host polling");
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#ifdef THREAD_POWER_THROTTLING_CURRENT_VERSION
            THREAD_POWER_THROTTLING_STATE power = {
                THREAD_POWER_THROTTLING_CURRENT_VERSION,
                THREAD_POWER_THROTTLING_EXECUTION_SPEED,
                THREAD_POWER_THROTTLING_EXECUTION_SPEED
            };
            SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &power, sizeof(power));
#endif
            run();
            m_Finished.store(true);
        });
    }

    void requestInterruption()
    {
        {
            std::lock_guard lock(m_WakeMutex);
            m_Interrupted.store(true);
        }
        m_Wake.notify_one();
    }

    bool isFinished() const { return m_Finished.load(); }
    void wait()
    {
        if (m_Thread.joinable()) m_Thread.join();
    }
#endif

private:
    bool tryPollComputer(QNetworkAccessManager* nam, NvAddress address, bool& changed)
    {
        NvHTTP http(address, 0, m_Computer->serverCert, !m_Computer->isNvidiaServerSoftware, nam, m_Computer->uuid);

        QString serverInfo;
        try {
            serverInfo = http.getServerInfo(NvHTTP::NvLogLevel::NVLL_NONE, true);
        } catch (...) {
            return false;
        }

        NvComputer newState(http, serverInfo);

        // Ensure the machine that responded is the one we intended to contact
        if (m_Computer->uuid != newState.uuid) {
            qInfo() << "Found unexpected PC" << newState.name << "looking for" << m_Computer->name;
            return false;
        }

        m_Computer->markAddressTestSucceeded(address);
        changed = m_Computer->update(newState);
        return true;
    }

    bool updateAppList(QNetworkAccessManager* nam, bool& changed)
    {
        NvHTTP http(m_Computer, nam);

        QVector<NvApp> appList;

        try {
            appList = http.getAppList();
            if (appList.isEmpty()) {
                return false;
            }
        } catch (...) {
            return false;
        }

        std::unique_lock lock(m_Computer->lock);
        changed = m_Computer->updateAppList(appList);
        return true;
    }

#ifdef LITE_GPUI_WORKER
    bool isInterruptionRequested() const { return m_Interrupted.load(); }
    void run()
#else
    void run() override
#endif
    {
#ifndef LITE_GPUI_WORKER
        // Reduce the power and performance impact of our
        // computer status polling while it's running.
        setPriority(QThread::LowPriority);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        setServiceLevel(QThread::QualityOfService::Eco);
#endif
#endif

        // Share the QNetworkAccessManager to conserve resources when polling.
        // Each instance creates a worker thread, so sharing them ensures that
        // we are not spamming a new thread for every single polling attempt.
        //
        // Since QThread inherit the priority of the current thread, this also
        // ensures that the NAM's worker thread will inherit our lower priority.
#ifdef LITE_NATIVE_HTTP
        QNetworkAccessManager* nam = nullptr;
#else
        QNetworkAccessManager network;
        auto* nam = &network;
#endif

        // Always fetch the applist the first time
        int pollsSinceLastAppListFetch = POLLS_PER_APPLIST_FETCH;
        while (!isInterruptionRequested()) {
            bool stateChanged = false;
            bool online = false;
            bool wasOnline = m_Computer->state == NvComputer::CS_ONLINE;
            for (int i = 0; i < (wasOnline ? TRIES_BEFORE_OFFLINING : 1) && !online; i++) {
                for (auto& address : m_Computer->uniqueAddresses()) {
                    if (isInterruptionRequested()) {
                        return;
                    }

                    if (tryPollComputer(nam, address, stateChanged)) {
                        if (!wasOnline) {
                            qInfo() << m_Computer->name << "is now online at" << m_Computer->activeAddress.toString();
                        }
                        online = true;
                        break;
                    }
                }
            }

            // Check if we failed after all retry attempts
            // Note: we don't need to acquire the read lock here,
            // because we're on the writing thread.
            if (!online && m_Computer->state != NvComputer::CS_OFFLINE) {
                qInfo() << m_Computer->name << "is now offline";
                m_Computer->state = NvComputer::CS_OFFLINE;
                stateChanged = true;
            }

            // Grab the applist if it's empty or it's been long enough that we need to refresh
            pollsSinceLastAppListFetch++;
            if (m_Computer->state == NvComputer::CS_ONLINE &&
                    m_Computer->pairState == NvComputer::PS_PAIRED &&
                    (m_Computer->appList.isEmpty() || pollsSinceLastAppListFetch >= POLLS_PER_APPLIST_FETCH)) {
                // Notify prior to the app list poll since it may take a while, and we don't
                // want to delay onlining of a machine, especially if we already have a cached list.
                if (stateChanged) {
                    m_Notify(m_Computer);
                    stateChanged = false;
                }

                if (updateAppList(nam, stateChanged)) {
                    pollsSinceLastAppListFetch = 0;
                }
            }

            if (stateChanged) {
                // Tell anyone listening that we've changed state
                m_Notify(m_Computer);
            }

#ifdef LITE_GPUI_WORKER
            std::unique_lock lock(m_WakeMutex);
            m_Wake.wait_for(lock, std::chrono::seconds(3), [this] { return isInterruptionRequested(); });
#else
            // Wait a bit to poll again, but do it in 100 ms chunks
            // so we can be interrupted reasonably quickly.
            // FIXME: QWaitCondition would be better.
            for (int i = 0; i < 30 && !isInterruptionRequested(); i++) {
                QThread::msleep(100);
            }
#endif
        }
    }

    NvComputer* m_Computer;
    std::function<void(NvComputer*)> m_Notify;
#ifdef LITE_GPUI_WORKER
    std::mutex m_WakeMutex;
    std::condition_variable m_Wake;
    std::atomic_bool m_Interrupted{false};
    std::atomic_bool m_Finished{false};
    std::thread m_Thread;
#endif
};

class ComputerPollingEntry
{
public:
    ComputerPollingEntry()
        : m_ActiveThread(nullptr)
    {

    }

    virtual ~ComputerPollingEntry()
    {
        interrupt();

        // interrupt() should have taken care of this
        Q_ASSERT(m_ActiveThread == nullptr);

        for (PcMonitorThread* thread : std::as_const(m_InactiveList)) {
            thread->wait();
            delete thread;
        }
    }

    bool isActive()
    {
        cleanInactiveList();

        return m_ActiveThread != nullptr;
    }

    void setActiveThread(PcMonitorThread* thread)
    {
        cleanInactiveList();

        Q_ASSERT(!isActive());
        m_ActiveThread = thread;
    }

    void interrupt()
    {
        cleanInactiveList();

        if (m_ActiveThread != nullptr) {
            // Interrupt the active thread
            m_ActiveThread->requestInterruption();

            // Place it on the inactive list awaiting death
            m_InactiveList.append(m_ActiveThread);

            m_ActiveThread = nullptr;
        }
    }

private:
    void cleanInactiveList()
    {
        QMutableListIterator<PcMonitorThread*> i(m_InactiveList);

        // Reap any threads that have finished
        while (i.hasNext()) {
            i.next();

            PcMonitorThread* thread = i.value();
            if (thread->isFinished()) {
                thread->wait();
                delete thread;
                i.remove();
            }
        }
    }

    PcMonitorThread* m_ActiveThread;
    QList<PcMonitorThread*> m_InactiveList;
};

ComputerManager::ComputerManager(StreamingPreferences* prefs, bool externalPolling, QString profileDirectory)
    : m_Prefs(prefs),
      m_ExternalPolling(externalPolling),
      m_ProfileDirectory(std::move(profileDirectory)),
      m_PollingRef(0),
#ifndef LITE_GPUI_WORKER
      m_MdnsBrowser(nullptr),
#endif
      m_NeedsDelayedFlush(false)
{
#ifdef LITE_NATIVE_WORKER_JSON
    bool imported = false;
    if (!m_ProfileDirectory.isEmpty()) {
        try {
            const std::filesystem::path directory(m_ProfileDirectory.toStdWString());
            auto saved = HostProfile::read(directory / L"hosts-v1.json");
            imported = !saved.has_value();
            auto hosts = saved ? std::move(*saved) : HostProfile::importLegacy(directory);
            for (auto& computer : hosts) {
                if (m_KnownHosts.contains(computer->uuid)) qFatal("Duplicate UUID in the GPUI host profile");
                computer->isSupportedServerVersion = true;
                computer->externalPort = computer->remoteAddress.port();
                m_LastSerializedHosts[computer->uuid] = *computer;
                m_KnownHosts.insert(computer->uuid, computer.get());
                computer.release();
            }
        } catch (const std::exception&) {
            qFatal("Unable to read the GPUI host profile");
        }
    } else
#endif
    {
#ifdef LITE_GPUI_WORKER
        qFatal("GPUI host storage requires an initialized worker profile");
#else
        loadLegacyHosts();
#endif
    }

    m_DelayedFlushThread = std::thread(&ComputerManager::runDelayedFlush, this);
#ifdef LITE_NATIVE_WORKER_JSON
    if (imported && !m_KnownHosts.isEmpty()) saveHosts();
#endif
#ifndef LITE_GPUI_WORKER
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &ComputerManager::handleAboutToQuit);
#endif
}

#ifndef LITE_GPUI_WORKER
void ComputerManager::loadLegacyHosts()
{
    QSettings settings;

    // If there's a hosts backup copy, we must have failed to commit
    // a previous update before exiting. Restore the backup now.
    int hosts = settings.beginReadArray(SER_HOSTS_BACKUP);
    if (hosts == 0) {
        // If there's no host backup, read from the primary location.
        settings.endArray();
        hosts = settings.beginReadArray(SER_HOSTS);
    }

    // Inflate our hosts from QSettings
    for (int i = 0; i < hosts; i++) {
        settings.setArrayIndex(i);
        NvComputer* computer = new NvComputer(settings);
        m_KnownHosts[computer->uuid] = computer;
        m_LastSerializedHosts[computer->uuid] = *computer;
    }
    settings.endArray();
}
#endif

ComputerManager::~ComputerManager()
{
#ifdef LITE_GPUI_WORKER
    beginShutdown();
    m_HostTasks.wait();
#endif
    // Stop the delayed flush thread before acquiring the lock in write mode
    // to avoid deadlocking with a flush that needs the lock in read mode.
    {
        // Wake the delayed flush thread
        {
            std::lock_guard lock(m_DelayedFlushMutex);
            m_DelayedFlushStopping = true;
        }
        m_DelayedFlushCondition.notify_one();

        // Wait for it to terminate (and finish any pending flush)
        m_DelayedFlushThread.join();

        // Delayed flushes should have completed by now
        Q_ASSERT(!m_NeedsDelayedFlush);
    }

    std::unique_lock lock(m_Lock);

#ifndef LITE_GPUI_WORKER
    // Delete machines that haven't been resolved yet
    while (!m_PendingResolution.isEmpty()) {
        MdnsPendingComputer* computer = m_PendingResolution.first();
        delete computer;
        m_PendingResolution.removeFirst();
    }

    // Delete the browser to stop discovery
    delete m_MdnsBrowser;
    m_MdnsBrowser = nullptr;
#endif

    // Interrupt polling
    for (ComputerPollingEntry* entry : std::as_const(m_PollEntries)) {
        entry->interrupt();
    }

    // Delete all polling entries (and associated threads)
    for (ComputerPollingEntry* entry : std::as_const(m_PollEntries)) {
        delete entry;
    }

    // Destroy all NvComputer objects now that polling is halted
    for (NvComputer* computer : std::as_const(m_KnownHosts)) {
        delete computer;
    }
}

void ComputerManager::runDelayedFlush() {
    for (;;) {
#ifdef LITE_NATIVE_WORKER_JSON
        std::vector<NvComputer> snapshot;
#endif
        // Wait for a delayed flush request or an interruption
        {
            std::unique_lock locker(m_DelayedFlushMutex);
            m_DelayedFlushCondition.wait(locker, [this] {
                return m_DelayedFlushStopping || m_NeedsDelayedFlush;
            });

            // Bail without flushing if we woke up for an interruption alone.
            // If we have both an interruption and a flush request, do the flush.
            if (!m_NeedsDelayedFlush) {
                Q_ASSERT(m_DelayedFlushStopping);
                break;
            }

            // Reset the delayed flush flag to ensure any racing saveHosts() call will set it again
            m_NeedsDelayedFlush = false;

            // Update the last serialized hosts map under the delayed flush mutex
            std::shared_lock hostsLock(m_Lock);
            m_LastSerializedHosts.clear();
            for (const NvComputer* computer : std::as_const(m_KnownHosts)) {
                // Copy the current state of the NvComputer to allow us to check later if we need
                // to serialize it again when attribute updates occur.
                std::shared_lock computerLock(computer->lock);
                m_LastSerializedHosts[computer->uuid] = *computer;
#ifdef LITE_NATIVE_WORKER_JSON
                if (!m_ProfileDirectory.isEmpty()) snapshot.push_back(*computer);
#endif
            }
        }

#ifdef LITE_NATIVE_WORKER_JSON
        if (!m_ProfileDirectory.isEmpty()) {
            try {
                HostProfile::save(std::filesystem::path(m_ProfileDirectory.toStdWString()) / L"hosts-v1.json", snapshot);
            } catch (const std::exception&) {
                qWarning("Unable to save the GPUI host profile");
                std::lock_guard lock(m_DelayedFlushMutex);
                m_LastSerializedHosts.clear();
            }
            continue;
        }
#endif

#ifndef LITE_GPUI_WORKER
        // Perform the flush
        {
            QSettings settings;

            // First, write to the backup location
            settings.beginWriteArray(SER_HOSTS_BACKUP);
            {
                std::shared_lock lock(m_Lock);
                int i = 0;
                for (const NvComputer* computer : std::as_const(m_KnownHosts)) {
                    settings.setArrayIndex(i++);
                    computer->serialize(settings, false);
                }
            }
            settings.endArray();

            // Next, write to the primary location
            settings.remove(SER_HOSTS);
            settings.beginWriteArray(SER_HOSTS);
            {
                std::shared_lock lock(m_Lock);
                int i = 0;
                for (const NvComputer* computer : std::as_const(m_KnownHosts)) {
                    settings.setArrayIndex(i++);
                    computer->serialize(settings, true);
                }
            }
            settings.endArray();

            // Finally, delete the backup copy
            settings.remove(SER_HOSTS_BACKUP);
        }
#endif
    }
}

void ComputerManager::saveHosts()
{
    Q_ASSERT(m_DelayedFlushThread.joinable());

    // Punt to a worker thread because QSettings on macOS can take ages (> 500 ms)
    // to persist our host list to disk (especially when a host has a bunch of apps).
    std::lock_guard locker(m_DelayedFlushMutex);
    m_NeedsDelayedFlush = true;
    m_DelayedFlushCondition.notify_one();
}

#ifndef LITE_GPUI_WORKER
QHostAddress ComputerManager::getBestGlobalAddressV6(QVector<QHostAddress> &addresses)
{
    for (const QHostAddress& address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            if (address.isInSubnet(QHostAddress("fe80::"), 10)) {
                // Link-local
                continue;
            }

            if (address.isInSubnet(QHostAddress("fec0::"), 10)) {
                qInfo() << "Ignoring site-local address:" << address;
                continue;
            }

            if (address.isInSubnet(QHostAddress("fc00::"), 7)) {
                qInfo() << "Ignoring ULA:" << address;
                continue;
            }

            if (address.isInSubnet(QHostAddress("2002::"), 16)) {
                qInfo() << "Ignoring 6to4 address:" << address;
                continue;
            }

            if (address.isInSubnet(QHostAddress("2001::"), 32)) {
                qInfo() << "Ignoring Teredo address:" << address;
                continue;
            }

            return address;
        }
    }

    return QHostAddress();
}
#endif

void ComputerManager::startPolling()
{
    std::unique_lock lock(m_Lock);

    if (++m_PollingRef > 1) {
        return;
    }

#ifndef LITE_GPUI_WORKER
    if (m_Prefs->enableMdns) {
        // Start an MDNS query for GameStream hosts
        m_MdnsServer.reset(new QMdnsEngine::Server());
        m_MdnsBrowser = new QMdnsEngine::Browser(m_MdnsServer.data(), "_nvstream._tcp.local.");
        connect(m_MdnsBrowser, &QMdnsEngine::Browser::serviceAdded,
                this, [this](const QMdnsEngine::Service& service) {
            qInfo() << "Discovered mDNS host:" << service.hostname();

            MdnsPendingComputer* pendingComputer = new MdnsPendingComputer(m_MdnsServer, service);
            connect(pendingComputer, &MdnsPendingComputer::resolvedHost,
                    this, &ComputerManager::handleMdnsServiceResolved);
            m_PendingResolution.append(pendingComputer);
        });
    }
    else {
        qWarning() << "mDNS is disabled by user preference";
    }
#endif

    // Start polling threads for each known host
    QMapIterator<QString, NvComputer*> i(m_KnownHosts);
    while (i.hasNext()) {
        i.next();
        startPollingComputer(i.value());
    }
}

// Must hold m_Lock for write
void ComputerManager::startPollingComputer(NvComputer* computer)
{
    if (m_ExternalPolling) return;
    if (m_PollingRef == 0) {
        return;
    }

    ComputerPollingEntry* pollingEntry;

    if (!m_PollEntries.contains(computer->uuid)) {
        pollingEntry = m_PollEntries[computer->uuid] = new ComputerPollingEntry();
    }
    else {
        pollingEntry = m_PollEntries[computer->uuid];
    }

    if (!pollingEntry->isActive()) {
        auto thread = std::make_unique<PcMonitorThread>(computer, [this](NvComputer* updated) {
            dispatchHostCompletion([this, updated] { handleComputerStateChanged(updated); });
        });
        thread->start();
        pollingEntry->setActiveThread(thread.release());
    }
}

#ifndef LITE_GPUI_WORKER
void ComputerManager::handleMdnsServiceResolved(MdnsPendingComputer* computer,
                                                QVector<QHostAddress>& addresses)
{
    QHostAddress v6Global = getBestGlobalAddressV6(addresses);
    bool added = false;

    // Add the host using the IPv4 address
    for (const QHostAddress& address : std::as_const(addresses)) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            // NB: We don't just call addNewHost() here with v6Global because the IPv6
            // address may not be reachable (if the user hasn't installed the IPv6 helper yet
            // or if this host lacks outbound IPv6 capability). We want to add IPv6 even if
            // it's not currently reachable.
            addNewHost(NvAddress(address, computer->port()),
                       true, computer->hostname(),
                       NvAddress(v6Global, computer->port()));
            added = true;
            break;
        }
    }

    if (!added) {
        // If we get here, there wasn't an IPv4 address so we'll do it v6-only
        for (const QHostAddress& address : std::as_const(addresses)) {
            if (address.protocol() == QAbstractSocket::IPv6Protocol) {
                // Use a link-local or site-local address for the "local address"
                if (address.isInSubnet(QHostAddress("fe80::"), 10) ||
                        address.isInSubnet(QHostAddress("fec0::"), 10) ||
                        address.isInSubnet(QHostAddress("fc00::"), 7)) {
                    addNewHost(NvAddress(address, computer->port()),
                               true, computer->hostname(),
                               NvAddress(v6Global, computer->port()));
                    break;
                }
            }
        }
    }

    m_PendingResolution.removeOne(computer);
    computer->deleteLater();
}
#endif

void ComputerManager::saveHost(NvComputer *computer)
{
    // If no serializable properties changed, don't bother saving hosts
    std::unique_lock lock(m_DelayedFlushMutex);
    std::shared_lock computerLock(computer->lock);
    if (!m_LastSerializedHosts.value(computer->uuid).isEqualSerialized(*computer)) {
        // Queue a request for a delayed flush to QSettings outside of the lock
        computerLock.unlock();
        lock.unlock();
        saveHosts();
    }
}

void ComputerManager::handleComputerStateChanged(NvComputer* computer)
{
    // A poll signal may already be queued when asynchronous deletion begins.
    // Do not dereference its pointer after ownership passes to the deletion task.
    if (m_DeletingHosts.contains(computer)) return;
#ifdef LITE_GPUI_WORKER
    m_HostEvents.stateChanged();
#else
    emit computerStateChanged(computer);
#endif

    if (computer->pendingQuit && computer->currentGameId == 0) {
        computer->pendingQuit = false;
        notifyQuitCompleted();
    }

    // Save updates to this host
    saveHost(computer);
}

QVector<NvComputer*> ComputerManager::getComputers()
{
    std::shared_lock lock(m_Lock);

    // Return a sorted host list
    auto hosts = QVector<NvComputer*>::fromList(m_KnownHosts.values());
    std::stable_sort(hosts.begin(), hosts.end(), [](const NvComputer* host1, const NvComputer* host2) {
        return host1->name.toLower() < host2->name.toLower();
    });
    return hosts;
}

class DeferredHostDeletionTask
{
public:
    DeferredHostDeletionTask(ComputerManager* cm, NvComputer* computer)
        : m_Computer(computer),
          m_ComputerManager(cm) {}

    void run()
    {
        ComputerPollingEntry* pollingEntry;
        const QString uuid = m_Computer->uuid;

        // Only do the minimum amount of work while holding the writer lock.
        // We must release it before calling saveHosts().
        {
            std::unique_lock lock(m_ComputerManager->m_Lock);

            pollingEntry = m_ComputerManager->m_PollEntries.take(m_Computer->uuid);

            m_ComputerManager->m_KnownHosts.remove(m_Computer->uuid);
        }

        // Persist the new host list with this computer deleted
        m_ComputerManager->saveHosts();

        // Delete the polling entry first. This will stop all polling threads too.
        delete pollingEntry;

        // Delete cached box art
        QDir boxArtCache(Path::getBoxArtCacheDir());
        if (boxArtCache.cd(uuid)) {
            boxArtCache.removeRecursively();
        }

        // Delete pairname mapping
        NvComputer::savePairname(m_Computer->uuid, QString());

        // Finally, delete the computer itself. This must be done
        // last because the polling thread might be using it.
        delete m_Computer;
        auto completion = [manager = m_ComputerManager, computer = m_Computer, uuid] {
            manager->finishHostDeletion(computer, uuid);
        };
        m_ComputerManager->dispatchHostCompletion(std::move(completion));
    }

private:
    NvComputer* m_Computer;
    ComputerManager* m_ComputerManager;
};

void ComputerManager::deleteHost(NvComputer* computer)
{
    if (m_DeletingHosts.contains(computer)) return;
    m_DeletingHosts.insert(computer);
    // Punt to a worker thread to avoid stalling the
    // UI while waiting for the polling thread to die
    submitHostTask([task = DeferredHostDeletionTask(this, computer)]() mutable { task.run(); });
}

void ComputerManager::finishHostDeletion(NvComputer* computer, const QString& uuid)
{
    m_DeletingHosts.remove(computer);
#ifdef LITE_GPUI_WORKER
    m_HostEvents.deleted(uuid);
#else
    emit hostDeleted(uuid);
#endif
}

void ComputerManager::notifyPairingCompleted(NvComputer* computer, const QString& error)
{
#ifdef LITE_GPUI_WORKER
    Q_UNUSED(computer);
    m_HostEvents.pairingCompleted(error);
#else
    emit pairingCompleted(computer, error);
#endif
}

void ComputerManager::notifyAddCompleted(bool success, bool portBlocking)
{
#ifdef LITE_GPUI_WORKER
    Q_UNUSED(portBlocking);
    m_HostEvents.addCompleted(success);
#else
    emit computerAddCompleted(success, portBlocking);
#endif
}

void ComputerManager::notifyQuitCompleted(const QString& error)
{
#ifdef LITE_GPUI_WORKER
    m_HostEvents.quitCompleted(error);
#else
    emit quitAppCompleted(error.isNull() ? QVariant() : QVariant(error));
#endif
}

void ComputerManager::submitHostTask(std::function<void()> task)
{
#ifdef LITE_GPUI_WORKER
    m_HostTasks.submit(std::move(task));
#else
    QThreadPool::globalInstance()->start(QRunnable::create(std::move(task)));
#endif
}

void ComputerManager::dispatchHostCompletion(std::function<void()> completion)
{
#ifdef LITE_GPUI_WORKER
    m_HostCompletionDispatcher(std::move(completion));
#else
    if (m_HostCompletionDispatcher) {
        m_HostCompletionDispatcher(std::move(completion));
    } else {
        QMetaObject::invokeMethod(this, std::move(completion), Qt::QueuedConnection);
    }
#endif
}

void ComputerManager::renameHost(NvComputer* computer, QString name)
{
    {
        std::unique_lock lock(computer->lock);

        computer->name = name;
        computer->hasCustomName = true;
    }

    // Notify the UI of the state change
    handleComputerStateChanged(computer);
}

void ComputerManager::clientSideAttributeUpdated(NvComputer* computer)
{
    // Notify the UI of the state change
    handleComputerStateChanged(computer);
}

#ifndef LITE_GPUI_WORKER
void ComputerManager::handleAboutToQuit()
{
    beginShutdown();
}
#endif

void ComputerManager::beginShutdown()
{
    {
        std::lock_guard lock(m_AddRetryMutex);
        m_Stopping = true;
    }
    m_AddRetryCondition.notify_all();
    interruptPolling();
}

bool ComputerManager::waitBeforeAddRetry()
{
    std::unique_lock lock(m_AddRetryMutex);
    return !m_AddRetryCondition.wait_for(lock, std::chrono::seconds(5), [this] { return m_Stopping.load(); });
}

void ComputerManager::interruptPolling()
{
    std::shared_lock lock(m_Lock);

    // Interrupt polling threads immediately, so they
    // avoid making additional requests while quitting
    for (ComputerPollingEntry* entry : std::as_const(m_PollEntries)) {
        entry->interrupt();
    }
}

class PendingPairingTask
{
    Q_DECLARE_TR_FUNCTIONS(PendingPairingTask)

public:
    PendingPairingTask(ComputerManager* computerManager, NvComputer* computer, QString pin)
        : m_ComputerManager(computerManager),
          m_Computer(computer),
          m_Pin(pin)
    {
    }

    void run()
    {
        NvPairingManager pairingManager(m_Computer);

        try {
           NvPairingManager::PairState result = pairingManager.pair(m_Computer->appVersion, m_Pin, m_Computer->serverCert);
           switch (result)
           {
           case NvPairingManager::PairState::PIN_WRONG:
               reportResult(tr("The PIN from the PC didn't match. Please try again."));
               break;
           case NvPairingManager::PairState::FAILED:
               if (m_Computer->currentGameId != 0) {
                   reportResult(tr("You cannot pair while a previous session is still running on the host PC. Quit any running games or reboot the host PC, then try pairing again."));
               }
               else {
                   reportResult(tr("Pairing failed. Please try again."));
               }
               break;
           case NvPairingManager::PairState::ALREADY_IN_PROGRESS:
               reportResult(tr("Another pairing attempt is already in progress."));
               break;
           case NvPairingManager::PairState::PAIRED:
               // Persist the newly pinned server certificate for this host
               m_ComputerManager->saveHost(m_Computer);

               reportResult(nullptr);
               break;
           }
        } catch (const GfeHttpResponseException& e) {
            reportResult(tr("GeForce Experience returned error: %1").arg(e.toQString()));
        } catch (const QtNetworkReplyException& e) {
            reportResult(e.toQString());
        }
    }

private:
    void reportResult(QString error)
    {
        m_ComputerManager->dispatchHostCompletion([manager = m_ComputerManager, computer = m_Computer, error] {
            manager->notifyPairingCompleted(computer, error);
        });
    }

    ComputerManager* m_ComputerManager;
    NvComputer* m_Computer;
    QString m_Pin;
};

void ComputerManager::pairHost(NvComputer* computer, QString pin)
{
    // Punt to a worker thread to avoid stalling the
    // UI while waiting for pairing to complete
    submitHostTask([task = PendingPairingTask(this, computer, std::move(pin))]() mutable { task.run(); });
}

class PendingQuitTask
{
    Q_DECLARE_TR_FUNCTIONS(PendingQuitTask)

public:
    PendingQuitTask(ComputerManager* computerManager, NvComputer* computer)
        : m_ComputerManager(computerManager),
          m_Computer(computer)
    {
    }

    void run()
    {
        NvHTTP http(m_Computer);

        try {
            if (m_Computer->currentGameId != 0) {
                http.quitApp();
            }
        } catch (const GfeHttpResponseException& e) {
            {
                std::unique_lock lock(m_Computer->lock);
                m_Computer->pendingQuit = false;
            }
            if (e.getStatusCode() == 599) {
                // 599 is a special code we make a custom message for
                reportFailure(tr("The running game wasn't started by this PC. "
                                      "You must quit the game on the host PC manually or use the device that originally started the game."));
            }
            else {
                reportFailure(e.toQString());
            }
        } catch (const QtNetworkReplyException& e) {
            {
                std::unique_lock lock(m_Computer->lock);
                m_Computer->pendingQuit = false;
            }
            reportFailure(e.toQString());
        }
    }

private:
    void reportFailure(QString error)
    {
        m_ComputerManager->dispatchHostCompletion([manager = m_ComputerManager, error] {
            manager->notifyQuitCompleted(error);
        });
    }

    ComputerManager* m_ComputerManager;
    NvComputer* m_Computer;
};

void ComputerManager::quitRunningApp(NvComputer* computer)
{
    std::unique_lock lock(computer->lock);
    computer->pendingQuit = true;

    submitHostTask([task = PendingQuitTask(this, computer)]() mutable { task.run(); });
}

void ComputerManager::stopPollingAsync()
{
    std::unique_lock lock(m_Lock);

    Q_ASSERT(m_PollingRef > 0);
    if (--m_PollingRef > 0) {
        return;
    }

#ifndef LITE_GPUI_WORKER
    // Delete machines that haven't been resolved yet
    while (!m_PendingResolution.isEmpty()) {
        MdnsPendingComputer* computer = m_PendingResolution.first();
        computer->deleteLater();
        m_PendingResolution.removeFirst();
    }

    // Delete the browser and server to stop discovery and refresh polling
    delete m_MdnsBrowser;
    m_MdnsBrowser = nullptr;
    m_MdnsServer.reset();
#endif

    // Interrupt all threads, but don't wait for them to terminate
    for (ComputerPollingEntry* entry : std::as_const(m_PollEntries)) {
        entry->interrupt();
    }
}

void ComputerManager::addNewHostManually(QString address)
{
    QUrl url = QUrl::fromUserInput("moonlight://" + address);
    if (url.isValid() && !url.host().isEmpty() && url.scheme() == "moonlight") {
        // If there wasn't a port specified, use the default
        addNewHost(NvAddress(url.host(), url.port(DEFAULT_HTTP_PORT)), false);
    }
#ifdef LITE_GPUI_WORKER
    else if (WindowsReachability::parseLiteral(address.trimmed().toStdWString()).size == 16) {
#else
    else if (QHostAddress(address).protocol() == QAbstractSocket::IPv6Protocol) {
#endif
        // The user specified an IPv6 literal without URL escaping, so use the default port
        addNewHost(NvAddress(address, DEFAULT_HTTP_PORT), false);
    }
    else {
        notifyAddCompleted(false, false);
    }
}

class PendingAddTask
{
    Q_DECLARE_TR_FUNCTIONS(PendingAddTask)

public:
    PendingAddTask(ComputerManager* computerManager, QString name, NvAddress address, NvAddress mdnsIpv6Address, bool mdns)
        : m_ComputerManager(computerManager),
          m_Name(name),
          m_Address(address),
          m_MdnsIpv6Address(mdnsIpv6Address),
          m_Mdns(mdns)
    {
    }

private:
    void reportResult(bool success, bool portBlocking)
    {
        m_ComputerManager->dispatchHostCompletion([manager = m_ComputerManager, success, portBlocking] {
            manager->notifyAddCompleted(success, portBlocking);
        });
    }

    void notifyChange(NvComputer* computer)
    {
        m_ComputerManager->dispatchHostCompletion([manager = m_ComputerManager, computer] {
            manager->handleComputerStateChanged(computer);
        });
    }

    QString fetchServerInfo(NvHTTP& http)
    {
        QString serverInfo;

        // Do nothing if we're quitting
        if (m_ComputerManager->m_Stopping) {
            return QString();
        }

        try {
            // There's a race condition between GameStream servers reporting presence over
            // mDNS and the HTTPS server being ready to respond to our queries. To work
            // around this issue, we will issue the request again after a few seconds if
            // we see a ServiceUnavailableError error.
            try {
                serverInfo = http.getServerInfo(NvHTTP::NVLL_VERBOSE);
            } catch (const QtNetworkReplyException& e) {
                if (e.getError() == HostRequestError::ServiceUnavailableError) {
                    qWarning() << "Retrying request in 5 seconds after ServiceUnavailableError";
                    if (!m_ComputerManager->waitBeforeAddRetry()) return QString();
                    serverInfo = http.getServerInfo(NvHTTP::NVLL_VERBOSE);
                    qInfo() << "Retry successful";
                }
                else {
                    // Rethrow other errors
                    throw e;
                }
            }
            return serverInfo;
        } catch (...) {
            if (!m_Mdns) {
                const unsigned int portTestResult = ML_TEST_RESULT_INCONCLUSIVE;

                reportResult(false, portTestResult != 0 && portTestResult != ML_TEST_RESULT_INCONCLUSIVE);
            }
            return QString();
        }
    }
    std::unique_ptr<NvComputer> fetchComputer(NvHTTP& http)
    {
        if (!NvHTTP::usesRustSessionApi()) {
            const auto xml = fetchServerInfo(http);
            return xml.isEmpty() ? nullptr : std::make_unique<NvComputer>(http, xml);
        }
        if (m_ComputerManager->m_Stopping) return nullptr;
        // Rust owns the request, parsing and cancellable 503 retry wait.
        try {
            return std::make_unique<NvComputer>(http, http.hostInformation());
        }
        catch (...) {
            if (!m_Mdns) reportResult(false, false);
            return nullptr;
        }
    }

public:
    void run()
    {
        // Use the placeholder UID for the initial poll, then we'll switch to the real one if it's not GFE
        NvHTTP http(m_Address, 0, HostCertificate(), false);

        if (m_Mdns) {
            if (m_MdnsIpv6Address.isNull()) {
                qInfo() << "Processing new PC" << m_Name << "from mDNS with local address" << m_Address.toString();
            }
            else {
                qInfo() << "Processing new PC" << m_Name << "from mDNS with local address" << m_Address.toString()
                        << "and IPv6 address" << m_MdnsIpv6Address.toString();
            }
        }
        else {
            qInfo() << "Processing new PC at" << m_Address.toString() << "from user";
        }

        // Perform initial serverinfo fetch over HTTP since we don't know which cert to use
        auto discovered = fetchComputer(http);
        if (!discovered && !m_MdnsIpv6Address.isNull()) {
            // Retry using the global IPv6 address if the IPv4 or link-local IPv6 address fails
            http.setAddress(m_MdnsIpv6Address);
            discovered = fetchComputer(http);
        }
        if (!discovered) {
            return;
        }

        // Create initial newComputer using HTTP serverinfo with no pinned cert
        NvComputer* newComputer = discovered.get();
        http.setTrueUid(!newComputer->isNvidiaServerSoftware);
        http.setHostUuid(newComputer->uuid);

        // Check if we have a record of this host UUID to pull the pinned cert
        NvComputer* existingComputer;
        {
            std::shared_lock lock(m_ComputerManager->m_Lock);
            existingComputer = m_ComputerManager->m_KnownHosts.value(newComputer->uuid);
            if (existingComputer != nullptr) {
                http.setServerCert(existingComputer->serverCert);
            }
        }

        // Fetch serverinfo again over HTTPS with the pinned cert
        if (existingComputer != nullptr) {
            Q_ASSERT(http.httpsPort() != 0);
            auto authenticated = fetchComputer(http);
            if (!authenticated) {
                return;
            }

            // Update the polled computer with the HTTPS information
            newComputer->update(*authenticated);
        }

        // Update addresses depending on the context
        if (m_Mdns) {
            // Only update local address if we actually reached the PC via this address.
            // If we reached it via the IPv6 address after the local address failed,
            // don't store the non-working local address.
            if (http.address() == m_Address) {
                newComputer->localAddress = m_Address;
            }

            if (!m_MdnsIpv6Address.isNull()) {
#ifndef LITE_GPUI_WORKER
                Q_ASSERT(QHostAddress(m_MdnsIpv6Address.address()).protocol() == QAbstractSocket::IPv6Protocol);
#endif
                newComputer->ipv6Address = m_MdnsIpv6Address;
            }
        }
        else {
            newComputer->manualAddress = m_Address;
        }

        NvComputer* updatedComputer = nullptr;
        QString updatedName;
        QString updatedAddress;
        {
            auto& hosts = m_ComputerManager->m_KnownHosts;
            auto& hostsMutex = m_ComputerManager->m_Lock;
            std::shared_lock readLock(hostsMutex);
            std::unique_lock writeLock(hostsMutex, std::defer_lock);
            NvComputer* existingComputer = hosts.value(newComputer->uuid);

            // The map only needs exclusive access when adding a host. Recheck
            // after releasing the read lock in case another task added it.
            if (existingComputer == nullptr) {
                readLock.unlock();
                writeLock.lock();
                existingComputer = hosts.value(newComputer->uuid);
            }

            if (existingComputer != nullptr) {
                if (existingComputer->update(*newComputer)) {
                    updatedComputer = existingComputer;
                    std::shared_lock hostLock(existingComputer->lock);
                    updatedName = existingComputer->name;
                    updatedAddress = existingComputer->activeAddress.toString();
                }
            }
            else {
                hosts[newComputer->uuid] = newComputer;
                discovered.release(); // Ownership passes to the host map.
                m_ComputerManager->startPollingComputer(newComputer);
                updatedComputer = newComputer;
            }
        }

        if (!m_Mdns) {
            reportResult(true, false);
        }
        if (updatedComputer != nullptr) {
            if (discovered) {
                qInfo() << updatedName << "is now at" << updatedAddress;
            }
            notifyChange(updatedComputer);
        }
    }

private:
    ComputerManager* m_ComputerManager;
    QString m_Name;
    NvAddress m_Address;
    NvAddress m_MdnsIpv6Address;
    bool m_Mdns;
};

void ComputerManager::addNewHost(NvAddress address, bool mdns, QString name, NvAddress mdnsIpv6Address)
{
    // Punt to a worker thread to avoid stalling the
    // UI while waiting for serverinfo query to complete
    submitHostTask([task = PendingAddTask(this, name, address, mdnsIpv6Address, mdns)]() mutable {
        task.run();
    });
}

QString ComputerManager::generatePinString()
{
#ifdef LITE_GPUI_WORKER
    constexpr unsigned pinCount = 10000;
    constexpr unsigned limit = (65536 / pinCount) * pinCount;
    uint16_t sample;
    do {
        if (RAND_bytes(reinterpret_cast<unsigned char*>(&sample), sizeof(sample)) != 1) {
            qFatal("Unable to generate a GameStream pairing PIN");
        }
    } while (sample >= limit);
    return QString::asprintf("%04u", sample % pinCount);
#else
    return QString::asprintf("%04u", QRandomGenerator::system()->bounded(10000));
#endif
}
