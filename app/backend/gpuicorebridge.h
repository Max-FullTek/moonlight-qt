#pragma once

#include <QObject>
#include <QJsonObject>
#include <QPointer>

class ComputerManager;
class NvComputer;
class Session;

// Protocol 1: newline-delimited JSON on inherited anonymous stdio pipes only.
// Commands: list, add(address), pair(host), start(host, app, settings), stop,
// shutdown. All host/session work runs on Qt's main thread. No QML engine is
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
    void shutdown();
    NvComputer* findHost(const QString& uuid);
    ComputerManager* m_Manager;
    QPointer<Session> m_Session;
    QString m_PairingHost;
    bool m_ShuttingDown = false;
    bool m_AddPending = false;
};
