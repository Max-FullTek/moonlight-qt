#include "backend/nvhttp.h"
#include "backend/nvpairingmanager.h"
#include "backend/qtnativehttppolicy.h"
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTimer>
#include <cstdio>

static QByteArray read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read public fixture");
    return file.readAll();
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 6) return 2;
    // The harness supplies a unique directory. Never open a user profile.
    QCoreApplication::setOrganizationName("NativeHttpFixture");
    QCoreApplication::setApplicationName("NvHTTP");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, argv[4]);
    QSettings settings;
    settings.setValue("certificate", read(QString(argv[3]) + "/server.pem"));
    settings.setValue("key", read(QString(argv[3]) + "/server-key.pem"));
    settings.setValue("uniqueid", "fixture-client");
    settings.sync();
    IdentityManager::get();
    QUrl url(QString::fromUtf8(argv[1]));
    QSslCertificate pin(read(argv[2]));
    NvHTTP http(NvAddress(url.host(), static_cast<uint16_t>(url.port())), static_cast<uint16_t>(url.port()), pin, true);
    if (qEnvironmentVariableIsSet("NATIVE_HTTP_FIXTURE_HTTP_PORT")) {
        http.m_BaseUrlHttp.setPort(qEnvironmentVariableIntValue("NATIVE_HTTP_FIXTURE_HTTP_PORT"));
    }
    const QString mode = QString::fromUtf8(argv[5]);
    int error = 0, status = 0;
    bool ok = false;
    QTimer::singleShot(0, &app, [&] {
        try {
            if (mode.startsWith("pair-")) {
                NvComputer computer;
                computer.activeAddress = NvAddress(url.host(), static_cast<uint16_t>(qEnvironmentVariableIntValue("NATIVE_HTTP_FIXTURE_HTTP_PORT")));
                computer.activeHttpsPort = static_cast<uint16_t>(url.port());
                computer.uuid = "pairing-fixture";
                const QSslCertificate previous(read(QString(argv[3]) + "/other.pem"));
                QSslCertificate result = previous;
                NvPairingManager pairing(&computer);
                const auto state = pairing.pair(mode == "pair-6" ? "6.0.0.0" : "7.0.0.0",
                                               qEnvironmentVariableIsSet("NATIVE_HTTP_WRONG_PIN") ? "0000" : "1234", result);
                error = static_cast<int>(state);
                ok = state == NvPairingManager::PAIRED ? result == pin : result == previous;
                if (state == NvPairingManager::PAIRED) {
                    ok = ok && NvComputer::getPairname(computer.uuid) == QStringLiteral("測試主機");
                }
            }
            else if (mode == "serverinfo") {
                ok = NvHTTP::getCurrentGame(http.getServerInfo(NvHTTP::NVLL_NONE)) == 0;
            }
            else if (mode == "json") {
                ok = http.configureAbr(true, 1000, 20000, QStringLiteral("繁中"), 500).value("ok").toBool();
            }
            else if (mode == "quit") { http.quitApp(); ok = true; }
            else if (mode == "image") { ok = !http.getBoxArt(42).isNull(); }
            else if (mode == "launch" || mode == "resume") {
                STREAM_CONFIGURATION config{};
                config.width = 1920; config.height = 1080; config.fps = 60;
                config.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
                RemoteStreamConfig remote(false, 0, 0, false, 0, 0, 0);
                QString session;
                http.startApp(mode, false, 42, &config, true, false, 1, false, session, -1,
                              std::nullopt, QStringLiteral("桌面"), remote);
                ok = session == QStringLiteral("rtsp://fixture/session");
            }
            else {
                if (mode == "cancel") QTimer::singleShot(150, &app, &QCoreApplication::quit);
                const auto text = http.openConnectionToString(http.m_BaseUrlHttps, mode, "x=1", mode == "cancel" ? 0 : 500, NvHTTP::NVLL_NONE);
                ok = !text.isEmpty();
            }
        }
        catch (const QtNetworkReplyException& exception) { error = exception.getError(); }
        catch (const GfeHttpResponseException& exception) { status = exception.getStatusCode(); }
        app.quit();
    });
    app.exec();
    std::printf("ok=%d error=%d status=%d\n", ok, error, status);
    return 0;
}
