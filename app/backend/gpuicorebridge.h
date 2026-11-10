#pragma once

#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <QHash>

class ComputerManager;
class NvComputer;
class Session;

// Protocol 1: newline-delimited JSON on inherited anonymous stdio pipes only.
// Commands: list, add, pair, rename, remove, quit, displays, app_preferences,
// start, stop, controls, shutdown, host_api_context (read-only profile adapter
// for Rust-owned displays), pair_rust/pair_name/pair_complete and monitor_update
// (typed Rust poll results), quit_rust/quit_complete (Rust cancel/verification).
// Host/session commands dispatch on Qt's main thread. No QML engine is
// created. stdout is reserved for protocol events; diagnostics stay on stderr.
class GpuiCoreBridge : public QObject
{
    Q_OBJECT
public:
    explicit GpuiCoreBridge(QObject* parent = nullptr);
    void start();
private:
    void command(const QByteArray& line);
    void send(QJsonObject event);
    void fail(const QString& code, const QString& message);
    void publishHosts();
    void publishMonitorContexts();
    void applyMonitorUpdate(const QJsonObject& request);
    void shutdown();
    NvComputer* findHost(const QString& uuid);
    ComputerManager* m_Manager;
    bool m_RustMonitor = false;
    int m_NextMonitorGeneration = 1;
    QHash<QString, QJsonObject> m_MonitorContexts;
    QPointer<Session> m_Session;
    QString m_PairingHost;
    int m_RustPairingRequest = 0;
    NvComputer* m_DeletingHost = nullptr;
    bool m_QuitPending = false;
    QString m_RustQuitHost;
    int m_RustQuitRequest = 0;
    bool m_ShuttingDown = false;
    bool m_AddPending = false;
};
