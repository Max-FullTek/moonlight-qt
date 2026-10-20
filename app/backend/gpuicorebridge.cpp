#include "gpuicorebridge.h"
#include "computermanager.h"
#include "streaming/session.h"
#include "settings/streamingpreferences.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QReadLocker>
#include <QWriteLocker>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <thread>

namespace {
constexpr int MaxLine = 64 * 1024;
bool integer(const QJsonObject& object, const char* key, int low, int high) {
    const auto value = object.value(QLatin1String(key));
    return value.isDouble() && value.toDouble() == value.toInt(-999999) &&
            value.toInt() >= low && value.toInt() <= high;
}
bool applySettings(const QJsonObject& value, StreamingPreferences* prefs) {
    if (!integer(value, "width", 256, 8192) || !integer(value, "height", 256, 8192) ||
        !integer(value, "fps", 1, 240) || !integer(value, "bitrate", 1, 150) ||
        !integer(value, "fec", -2, 100) || !integer(value, "scale_percent", 20, 100) ||
        !integer(value, "codec", 0, 3) || !value["abr"].isBool() ||
        !value["enhancement"].isBool() || !value["scale"].isBool()) return false;
    // Display options are additive in protocol 1. Older frontends retain the
    // first integration's explicit defaults; malformed new options are rejected
    // before any preferences are changed.
    QJsonObject display{{"window_mode", 2}, {"decoder", 0}, {"hdr", false},
                        {"vsync", true}, {"frame_pacing", false}};
    if (value.contains("display")) {
        if (!value["display"].isObject()) return false;
        display = value["display"].toObject();
        if (!integer(display, "window_mode", 0, 2) || !integer(display, "decoder", 0, 2) ||
            !display["hdr"].isBool() || !display["vsync"].isBool() || !display["frame_pacing"].isBool()) return false;
    }
    prefs->width = value["width"].toInt();
    prefs->height = value["height"].toInt();
    prefs->fps = value["fps"].toInt();
    prefs->bitrateKbps = value["bitrate"].toInt() * 1000;
    prefs->fecPercentage = value["fec"].toInt();
    prefs->enableSunshineAbr = value["abr"].toBool();
    prefs->autoAdjustBitrate = false;
    const StreamingPreferences::VideoCodecConfig codecs[] = {
        StreamingPreferences::VCC_AUTO, StreamingPreferences::VCC_FORCE_H264,
        StreamingPreferences::VCC_FORCE_HEVC, StreamingPreferences::VCC_FORCE_AV1 };
    prefs->videoCodecConfig = codecs[value["codec"].toInt()];
    prefs->videoEnhancement = value["enhancement"].toBool();
    prefs->streamResolutionScale = value["scale"].toBool();
    prefs->streamResolutionScaleRatio = value["scale_percent"].toInt();
    prefs->windowMode = static_cast<StreamingPreferences::WindowMode>(display["window_mode"].toInt());
    prefs->videoDecoderSelection = static_cast<StreamingPreferences::VideoDecoderSelection>(display["decoder"].toInt());
    prefs->enableVsync = display["vsync"].toBool();
    prefs->framePacing = display["frame_pacing"].toBool();
    // No host app is ever quit implicitly, including on parent disconnect.
    prefs->quitAppAfter = false;
    prefs->enableHdr = display["hdr"].toBool();
    return true;
}
}

GpuiCoreBridge::GpuiCoreBridge(QObject* parent) : QObject(parent) {
    auto* prefs = StreamingPreferences::get();
    prefs->enableMdns = false;
    prefs->richPresence = false;
    prefs->detectNetworkBlocking = false;
    prefs->quitAppAfter = false;
    m_Manager = new ComputerManager(prefs);
    m_Manager->setParent(this);
    connect(m_Manager, &ComputerManager::computerStateChanged, this, [this] { publishHosts(); });
    connect(m_Manager, &ComputerManager::computerAddCompleted, this, [this](QVariant success, QVariant) {
        m_AddPending = false;
        if (!success.toBool()) fail("add_failed", tr("Unable to connect to the specified host."));
        else send({{"event", "added"}});
        publishHosts();
    });
    connect(m_Manager, &ComputerManager::pairingCompleted, this, [this](NvComputer*, QString error) {
        m_PairingHost.clear();
        if (error.isEmpty()) send({{"event", "paired"}});
        else fail("pair_failed", error);
        publishHosts();
    });
}

void GpuiCoreBridge::send(QJsonObject event) {
    event.insert("protocol", 1);
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    if (fwrite(line.constData(), 1, size_t(line.size()), stdout) != size_t(line.size()) || fflush(stdout) != 0) {
        shutdown();
    }
}
void GpuiCoreBridge::fail(const QString& code, const QString& message) {
    send({{"event", "error"}, {"code", code}, {"message", message}});
}
void GpuiCoreBridge::start() {
    send({{"event", "ready"}, {"qml_loaded", false}});
    publishHosts();
    m_Manager->startPolling();
    // The reader owns no Qt/network state and exits on shutdown or pipe EOF.
    // The process owns this thread; the parent closes stdin during teardown.
    std::thread([this] {
        QByteArray line;
        bool oversized = false;
        int ch;
        while ((ch = fgetc(stdin)) != EOF) {
            if (ch == '\n') {
                const QByteArray request = oversized ? QByteArray() : line;
                QMetaObject::invokeMethod(this, [this, request, oversized] {
                    if (oversized) fail("request_too_large", tr("Request exceeds 64 KiB."));
                    else command(request);
                }, Qt::QueuedConnection);
                if (!oversized && QJsonDocument::fromJson(line).object().value("command").toString() == "shutdown") return;
                line.clear(); oversized = false;
            } else if (line.size() < MaxLine) line.append(char(ch));
            else oversized = true;
        }
        QMetaObject::invokeMethod(this, [this] { shutdown(); }, Qt::QueuedConnection);
    }).detach();
}
void GpuiCoreBridge::publishHosts() {
    QJsonArray hosts;
    for (auto* host : m_Manager->getComputers()) {
        QReadLocker lock(&host->lock);
        QJsonArray apps;
        for (const auto& app : host->appList) apps.append(QJsonObject{{"id", app.id}, {"name", app.name}, {"hidden", app.hidden}, {"direct_launch", app.directLaunch}});
        const auto address = host->manualAddress.isNull() ? host->activeAddress : host->manualAddress;
        hosts.append(QJsonObject{{"id", host->uuid}, {"name", host->name}, {"address", address.toString()},
                                 {"online", host->state == NvComputer::CS_ONLINE},
                                 {"paired", host->pairState == NvComputer::PS_PAIRED},
                                 {"current_app", host->currentGameId}, {"apps", apps}});
    }
    send({{"event", "hosts"}, {"hosts", hosts}});
}
NvComputer* GpuiCoreBridge::findHost(const QString& uuid) {
    for (auto* host : m_Manager->getComputers()) if (host->uuid == uuid) return host;
    return nullptr;
}
void GpuiCoreBridge::shutdown() {
    if (m_ShuttingDown) return;
    m_ShuttingDown = true;
    if (m_Session) m_Session->interrupt();
    else QCoreApplication::quit();
}
void GpuiCoreBridge::command(const QByteArray& line) {
    if (m_ShuttingDown) return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail("invalid_json", tr("Expected a JSON object.")); return;
    }
    const auto request = document.object();
    const auto name = request.value("command").toString();
    if (name == "shutdown") { shutdown(); return; }
    if (name == "list") { publishHosts(); return; }
    if (name == "stop") { if (m_Session) m_Session->interrupt(); return; }
    if (m_Session || m_AddPending || !m_PairingHost.isEmpty()) { fail("busy", tr("Another operation is still active.")); return; }
    if (name == "add") {
        const auto address = request.value("address").toString().trimmed();
        if (address.isEmpty() || address.size() > 260 || address.contains(QRegularExpression("[\\s/\\\\?#@]"))) {
            fail("invalid_address", tr("Enter an IP address or hostname, with an optional port.")); return;
        }
        m_AddPending = true;
        m_Manager->addNewHostManually(address);
        return;
    }
    if (name != "pair" && name != "start" && name != "app_preferences") { fail("unknown_command", tr("Unknown command.")); return; }
    auto* host = findHost(request.value("host").toString());
    if (!host) { fail("host_not_found", tr("Host not found.")); return; }
    if (name == "app_preferences") {
        if (!request["hidden"].isBool() || !request["direct_launch"].isBool() ||
            !integer(request, "app", 1, 2147483647) ||
            (request["hidden"].toBool() && request["direct_launch"].toBool())) {
            fail("invalid_app_preferences", tr("Application preferences are invalid.")); return;
        }
        {
            QWriteLocker lock(&host->lock);
            NvApp* selected = nullptr;
            for (auto& app : host->appList) if (app.id == request["app"].toInt()) selected = &app;
            if (!selected) { fail("app_not_found", tr("Application not found.")); return; }
            if (request["hidden"].toBool() && host->currentGameId == selected->id) {
                fail("app_running", tr("A running application cannot be hidden.")); return;
            }
            selected->hidden = request["hidden"].toBool();
            selected->directLaunch = request["direct_launch"].toBool();
            if (selected->directLaunch) {
                for (auto& app : host->appList) if (&app != selected) app.directLaunch = false;
            }
        }
        m_Manager->clientSideAttributeUpdated(host);
        return;
    }
    if (name == "pair") {
        { QReadLocker lock(&host->lock);
          if (host->pairState == NvComputer::PS_PAIRED) { send({{"event", "paired"}}); return; }
          if (host->state != NvComputer::CS_ONLINE) { fail("offline", tr("Host is offline.")); return; } }
        m_PairingHost = host->uuid;
        const QString pin = m_Manager->generatePinString();
        send({{"event", "pairing"}, {"host", host->uuid}, {"pin", pin}});
        m_Manager->pairHost(host, pin);
        return;
    }
    NvApp selected;
    { QReadLocker lock(&host->lock);
      if (host->pairState != NvComputer::PS_PAIRED || host->state != NvComputer::CS_ONLINE) {
          fail("not_ready", tr("Host must be online and paired.")); return;
      }
      for (const auto& app : host->appList) if (app.id == request.value("app").toInt()) selected = app;
      if (!selected.isInitialized()) { fail("app_not_found", tr("Application not found.")); return; }
      if (host->currentGameId != 0 && host->currentGameId != selected.id) {
          fail("host_busy", tr("Another app is running on the host. End it there before switching.")); return;
      } }
    auto* preferences = StreamingPreferences::get();
    if (!applySettings(request.value("settings").toObject(), preferences)) {
        fail("invalid_settings", tr("Streaming settings are invalid.")); return;
    }
    auto* session = new Session(host, selected, preferences);
    m_Session = session;
    connect(session, &Session::stageStarting, this, [this](QString stage) { send({{"event", "stage"}, {"stage", stage}}); });
    connect(session, &Session::stageFailed, this, [this](QString stage, int code, QString) {
        fail("stage_failed", tr("%1 failed (%2)").arg(stage).arg(code));
    });
    connect(session, &Session::displayLaunchError, this, [this](QString message) { fail("launch_failed", message); });
    connect(session, &Session::connectionStarted, this, [this] { send({{"event", "stream_started"}}); });
    connect(session, &Session::readyForDeletion, this, [this, session] {
        m_Session = nullptr;
        session->deleteLater();
        send({{"event", "stream_finished"}});
        publishHosts();
        if (m_ShuttingDown) QCoreApplication::quit();
    }, Qt::QueuedConnection);
    if (!session->initialize(nullptr)) {
        m_Session = nullptr;
        session->deleteLater();
        send({{"event", "stream_finished"}});
        return;
    }
    send({{"event", "launching"}});
    session->start();
}
