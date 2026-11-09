#include "gpuicorebridge.h"
#include "computermanager.h"
#include "nvhttp.h"
#include "streaming/streamutils.h"
#include "SDL_compat.h"
#include <cmath>
#include <QDebug>
#include "streaming/session.h"
#include "settings/streamingpreferences.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QReadLocker>
#include <QWriteLocker>
#include <QRegularExpression>
#include <QThreadPool>
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
// Validate all retained settings before mutating preferences. Older protocol-1
// frontends may omit the advanced object and retain their existing defaults.
bool validAdvanced(const QJsonObject& a) {
    const QStringList allowed = {"host_audio","audio","mute_background","screen_mode","vdd","optimize","quit_host","desktop_mouse","local_cursor","capture","swap_mouse","swap_alt","reverse_scroll","touchpad","precision_touchpad","swap_face","deadzone","quit_combo","force_gamepad","gamepad_mouse","background_gamepad","haptics","stats","overlay","yuv444","connection_warnings","configuration_warnings","keep_awake","ui_window","remember_window","hdr_format","hdr_brightness","hdr_peak","hdr_min","hdr_average","client_display","custom_width","custom_height","custom_fps","host_display"};
    for (auto it=a.begin();it!=a.end();++it) if (!allowed.contains(it.key())) return false;
    if (!(a["host_audio"].isBool())) return false;
    if (!(integer(a,"audio",0,3))) return false;
    if (!(a["mute_background"].isBool())) return false;
    if (!(integer(a,"screen_mode",0,5))) return false;
    if (!(integer(a,"vdd",0,2))) return false;
    if (!(a["optimize"].isBool())) return false;
    if (!(a["quit_host"].isBool())) return false;
    if (!(a["desktop_mouse"].isBool())) return false;
    if (!(a["local_cursor"].isBool())) return false;
    if (!(integer(a,"capture",0,2))) return false;
    if (!(a["swap_mouse"].isBool())) return false;
    if (!(a["swap_alt"].isBool())) return false;
    if (!(a["reverse_scroll"].isBool())) return false;
    if (!(a["touchpad"].isBool())) return false;
    if (!(a["precision_touchpad"].isBool())) return false;
    if (!(a["swap_face"].isBool())) return false;
    if (!(integer(a,"deadzone",0,30))) return false;
    if (!(integer(a,"quit_combo",0,5))) return false;
    if (!(a["force_gamepad"].isBool())) return false;
    if (!(a["gamepad_mouse"].isBool())) return false;
    if (!(a["background_gamepad"].isBool())) return false;
    if (!(integer(a,"haptics",0,1))) return false;
    if (!(a["stats"].isBool())) return false;
    if (!(integer(a,"overlay",0,4))) return false;
    if (!(a["yuv444"].isBool())) return false;
    if (!(a["connection_warnings"].isBool())) return false;
    if (!(a["configuration_warnings"].isBool())) return false;
    if (!(a["keep_awake"].isBool())) return false;
    if (!(integer(a,"ui_window",0,2))) return false;
    if (!(a["remember_window"].isBool())) return false;
    if (!(integer(a,"hdr_format",0,1))) return false;
    if (!(integer(a,"hdr_brightness",0,2))) return false;
    if (!((a["hdr_peak"].isDouble() && std::isfinite(a["hdr_peak"].toDouble()) && a["hdr_peak"].toDouble()>=1 && a["hdr_peak"].toDouble()<=10000))) return false;
    if (!((a["hdr_min"].isDouble() && std::isfinite(a["hdr_min"].toDouble()) && a["hdr_min"].toDouble()>=0 && a["hdr_min"].toDouble()<=10))) return false;
    if (!((a["hdr_average"].isDouble() && std::isfinite(a["hdr_average"].toDouble()) && a["hdr_average"].toDouble()>=1 && a["hdr_average"].toDouble()<=10000))) return false;
    if (!(integer(a,"client_display",0,16))) return false;
    if (!(integer(a,"custom_width",256,8192))) return false;
    if (!(integer(a,"custom_height",256,8192))) return false;
    if (!(integer(a,"custom_fps",1,512))) return false;
    if (!((a["host_display"].isString() && a["host_display"].toString().size()<=256 && !a["host_display"].toString().contains(QRegularExpression("[\\x00-\\x1f\\x7f]"))))) return false;
    return a["hdr_min"].toDouble()<=a["hdr_average"].toDouble() && a["hdr_average"].toDouble()<=a["hdr_peak"].toDouble();
}
void applyAdvanced(const QJsonObject& a, StreamingPreferences* prefs) {
    prefs->playAudioOnHost = a["host_audio"].toBool();
    prefs->audioConfig = static_cast<decltype(prefs->audioConfig)>(a["audio"].toInt());
    prefs->muteOnFocusLoss = a["mute_background"].toBool();
    prefs->screenCombinationMode = static_cast<decltype(prefs->screenCombinationMode)>(a["screen_mode"].toInt()-1);
    prefs->gameOptimizations = a["optimize"].toBool();
    prefs->quitAppAfter = a["quit_host"].toBool();
    prefs->absoluteMouseMode = a["desktop_mouse"].toBool();
    prefs->showLocalCursor = a["local_cursor"].toBool();
    prefs->captureSysKeysMode = static_cast<decltype(prefs->captureSysKeysMode)>(a["capture"].toInt());
    prefs->swapMouseButtons = a["swap_mouse"].toBool();
    prefs->swapWinAltKeys = a["swap_alt"].toBool();
    prefs->reverseScrollDirection = a["reverse_scroll"].toBool();
    prefs->absoluteTouchMode = !a["touchpad"].toBool();
    prefs->enableNativeTouchpad = a["precision_touchpad"].toBool();
    prefs->swapFaceButtons = a["swap_face"].toBool();
    prefs->gamepadDeadzone = a["deadzone"].toInt();
    prefs->gamepadQuitCombo = static_cast<decltype(prefs->gamepadQuitCombo)>((a["quit_combo"].toInt()==0 ? 0 : a["quit_combo"].toInt()+1));
    prefs->multiController = !a["force_gamepad"].toBool();
    prefs->gamepadMouse = a["gamepad_mouse"].toBool();
    prefs->backgroundGamepad = a["background_gamepad"].toBool();
    prefs->dualSenseHapticsMode = static_cast<decltype(prefs->dualSenseHapticsMode)>(a["haptics"].toInt());
    prefs->showPerformanceOverlay = a["stats"].toBool();
    prefs->setOverlayMenuPosition(static_cast<StreamingPreferences::OverlayMenuPosition>(a["overlay"].toInt()));
    prefs->enableYUV444 = a["yuv444"].toBool();
    prefs->connectionWarnings = a["connection_warnings"].toBool();
    prefs->configurationWarnings = a["configuration_warnings"].toBool();
    prefs->keepAwake = a["keep_awake"].toBool();
    prefs->hdrMode = static_cast<decltype(prefs->hdrMode)>(a["hdr_format"].toInt()+1);
    prefs->hdrBrightnessMode = static_cast<decltype(prefs->hdrBrightnessMode)>(a["hdr_brightness"].toInt());
    prefs->hdrMaxBrightness = a["hdr_peak"].toDouble();
    prefs->hdrMinBrightness = a["hdr_min"].toDouble();
    prefs->hdrMaxAverageBrightness = a["hdr_average"].toDouble();
}
bool applySettings(const QJsonObject& value, StreamingPreferences* prefs) {
    if (!integer(value, "width", 256, 8192) || !integer(value, "height", 256, 8192) ||
        !integer(value, "fps", 1, 512) || !integer(value, "bitrate", 1, 150) ||
        !integer(value, "fec", -2, 100) || !integer(value, "scale_percent", 20, 100) ||
        !integer(value, "codec", 0, 3) || !value["abr"].isBool() ||
        !value["enhancement"].isBool() || !value["scale"].isBool()) return false;
    if (value.contains("advanced") && (!value["advanced"].isObject() || !validAdvanced(value["advanced"].toObject()))) return false;
    if (value.contains("auto_resolution") && !value["auto_resolution"].isBool()) return false;
    if (value.contains("language") && !integer(value, "language", 0, 2)) return false;
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
    // Quit-after-stream is opt-in through the retained advanced setting.
    prefs->quitAppAfter = false;
    prefs->enableHdr = display["hdr"].toBool();
    if (value.contains("advanced")) applyAdvanced(value["advanced"].toObject(),prefs);
    if (value.contains("language")) {
        const StreamingPreferences::Language languages[] = {
            StreamingPreferences::LANG_ZH_TW, StreamingPreferences::LANG_ZH_CN, StreamingPreferences::LANG_EN
        };
        const auto language = languages[value["language"].toInt()];
        if (prefs->language != language) {
            prefs->language = language;
            prefs->retranslate();
        }
    }
    // These features remain excluded even when an older profile enabled them.
    prefs->enableMicrophone=false; prefs->ignoreAspectRatio=false;
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
    connect(m_Manager, &ComputerManager::hostDeleted, this, [this](const QString& uuid) {
        m_DeletingHost = nullptr;
        send({{"event", "host_removed"}, {"host", uuid}});
        publishHosts();
    });
    connect(m_Manager, &ComputerManager::quitAppCompleted, this, [this](QVariant error) {
        if (!m_QuitPending) return;
        m_QuitPending = false;
        if (error.isNull() || error.toString().isEmpty()) send({{"event", "app_quit"}});
        else fail("quit_failed", error.toString());
        publishHosts();
    });
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
        else {
            const auto code=error.contains("PIN",Qt::CaseInsensitive)?"pair_pin_mismatch":error.contains("already",Qt::CaseInsensitive)?"pair_already_active":error.contains("timed out",Qt::CaseInsensitive)?"pair_timeout":"pair_failed";
            fail(code, error);
        }
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
    qWarning() << "GPUI operation failed:" << code;
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
                Session::wakeForCoreCommands();
                if (!oversized && QJsonDocument::fromJson(line).object().value("command").toString() == "shutdown") return;
                line.clear(); oversized = false;
            } else if (line.size() < MaxLine) line.append(char(ch));
            else oversized = true;
        }
        QMetaObject::invokeMethod(this, [this] { shutdown(); }, Qt::QueuedConnection);
        Session::wakeForCoreCommands();
    }).detach();
}
void GpuiCoreBridge::publishHosts() {
    QJsonArray hosts;
    for (auto* host : m_Manager->getComputers()) {
        if (host == m_DeletingHost) continue;
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
    for (auto* host : m_Manager->getComputers()) if (host != m_DeletingHost && host->uuid == uuid) return host;
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
    if (name == "controls") {
        if (!request["action"].isString() ||
                (request.contains("value") && !integer(request, "value", 1000, 150000)) ||
                (request.contains("request_id") && !integer(request, "request_id", 1, 2147483647))) {
            fail("invalid_controls", tr("Invalid control action.")); return;
        }
        if (!m_Session || !m_Session->handleGpuiControl(request["action"].toString(), request["value"].toInt(), request["request_id"].toInt())) {
            fail("controls_unavailable", tr("Controls are unavailable or stream focus could not be restored."));
        }
        return;
    }
    if (m_Session || m_AddPending || m_DeletingHost || m_QuitPending || !m_PairingHost.isEmpty()) { fail("busy", tr("Another operation is still active.")); return; }
    if (name == "add") {
        const auto address = request.value("address").toString().trimmed();
        if (address.isEmpty() || address.size() > 260 || address.contains(QRegularExpression("[\\s/\\\\?#@]"))) {
            fail("invalid_address", tr("Enter an IP address or hostname, with an optional port.")); return;
        }
        m_AddPending = true;
        m_Manager->addNewHostManually(address);
        return;
    }
    if (name != "pair" && name != "start" && name != "app_preferences" && name != "displays" &&
            name != "rename" && name != "remove" && name != "quit") { fail("unknown_command", tr("Unknown command.")); return; }
    auto* host = findHost(request.value("host").toString());
    if (!host) { fail("host_not_found", tr("Host not found.")); return; }
    if (name == "rename") {
        const auto label = request["name"].toString().trimmed();
        if (label.isEmpty() || label.size() > 128 || label.contains(QRegularExpression("[\\x00-\\x1f\\x7f]"))) {
            fail("invalid_name", tr("Enter a host name between 1 and 128 characters.")); return;
        }
        m_Manager->renameHost(host, label);
        send({{"event", "host_renamed"}});
        return;
    }
    if (name == "remove") {
        m_DeletingHost = host;
        m_Manager->deleteHost(host);
        return;
    }
    if (name == "quit") {
        { QReadLocker lock(&host->lock);
          if (host->pairState != NvComputer::PS_PAIRED || host->state != NvComputer::CS_ONLINE) {
              fail("not_ready", tr("Host must be online and paired.")); return;
          }
          if (host->currentGameId == 0) { send({{"event", "app_quit"}}); return; }
        }
        m_QuitPending = true;
        m_Manager->quitRunningApp(host);
        return;
    }
    if (name == "displays") {
        NvAddress address;
        uint16_t httpsPort;
        QSslCertificate certificate;
        QString uuid;
        bool useTrueUid;
        {
            QReadLocker lock(&host->lock);
            if (host->pairState != NvComputer::PS_PAIRED || host->state != NvComputer::CS_ONLINE) {
                fail("not_ready", tr("Host must be online and paired.")); return;
            }
            address = host->activeAddress;
            httpsPort = host->activeHttpsPort;
            certificate = host->serverCert;
            uuid = host->uuid;
            useTrueUid = !host->isNvidiaServerSoftware;
        }
        m_AddPending = true;
        // The existing application shutdown waits for this pool. Snapshot the
        // connection under the host lock; the HTTP worker owns no host pointer.
        QThreadPool::globalInstance()->start(QRunnable::create([this, address, httpsPort, certificate, uuid, useTrueUid] {
            NvHTTP http(address, httpsPort, certificate, useTrueUid, nullptr, uuid);
            const auto displays = QJsonArray::fromVariantList(http.getDisplays());
            QMetaObject::invokeMethod(this, [this, uuid, displays] {
                m_AddPending = false;
                send({{"event", "displays"}, {"host", uuid}, {"displays", displays}});
            }, Qt::QueuedConnection);
        }));
        return;
    }
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
    const auto settings=request["settings"].toObject();
    if (request.contains("client_pid") && !integer(request, "client_pid", 1, 2147483647)) {
        fail("invalid_controls", tr("Invalid client process.")); return;
    }
    const auto advanced=settings["advanced"].toObject();
    int clientDisplay=advanced.value("client_display").toInt(0)-1;
    if (clientDisplay<0) {
        clientDisplay=0;
        const auto center=request["client_center"].toObject();
        if (center.contains("x") && center.contains("y")) {
            const double x=center["x"].toDouble(),y=center["y"].toDouble();
            for (int i=0;i<SDL_GetNumVideoDisplays();++i) {SDL_Rect r;if (SDL_GetDisplayBounds(i,&r)==0 && x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {clientDisplay=i;break;}}
        }
    }
    if (clientDisplay>=SDL_GetNumVideoDisplays()) {fail("display_unavailable",tr("Selected client monitor is no longer available."));return;}
    if (settings["auto_resolution"].toBool()) {
        SDL_DisplayMode mode;SDL_Rect safeArea;
        if (!StreamUtils::getNativeDesktopMode(clientDisplay,&mode,&safeArea) || mode.w<256 || mode.h<256 || mode.w>8192 || mode.h>8192) {fail("display_mode_unavailable",tr("Unable to read the selected client monitor resolution."));return;}
        preferences->width=mode.w;preferences->height=mode.h;
        // Existing launch mode negotiation requests the matching host/VDD mode.
        preferences->remoteResolution=false;
    }
    const int vdd=advanced["vdd"].toInt();
    auto* session = new Session(host, selected, preferences,advanced["host_display"].toString(),vdd==0?std::nullopt:std::optional<bool>(vdd==2));
    session->setClientDisplay(clientDisplay);
    session->setGpuiControlsClient(static_cast<unsigned long>(request["client_pid"].toInt()));
    m_Session = session;
    connect(session, &Session::hostQuitFailed, this, [this](const QString& message) {
        fail("host_quit_failed", message);
    });
    connect(session, &Session::gpuiControlsChanged, this, [this](bool active, bool mouse, bool gamepad) {
        send({{"event", "controls"}, {"active", active}, {"gamepad_mouse", mouse}, {"has_gamepad", gamepad}});
    });
    connect(session, &Session::gpuiControlsNavigate, this, [this](const QString& direction) {
        send({{"event", "controls_navigate"}, {"direction", direction}});
    });
    connect(session, &Session::gpuiBitrateResult, this, [this, session](int requestId, int requested, bool success, const QString& error) {
        if (m_Session != session) return;
        send({{"event", "bitrate_result"}, {"request_id", requestId}, {"requested_kbps", requested},
              {"success", success}, {"message", error}});
    });
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
