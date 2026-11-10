#include "nvcomputer.h"
#include "pairedcertificate.h"
#include "sessionapirpc.h"
#include <Limelight.h>

#include <QHostInfo>
#include <QDebug>
#include <QUuid>
#include <QtNetwork/QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QXmlStreamReader>
#include <QSslKey>
#include <QImageReader>
#include <QtEndian>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <QBuffer>
#ifdef LITE_NATIVE_HTTP
#include "qtnativehttptransport.h"
#include "qtnativehttppolicy.h"
#endif

bool NvHTTP::usesRustSessionApi() { return SessionApiRpc::broker().enabled(); }
QJsonObject NvHTTP::rustSessionCall(QJsonObject request,unsigned timeoutMs) {
    auto clientName=NvComputer::getPairname(m_Uuid);
    if (clientName.isEmpty()) clientName=QHostInfo::localHostName();
    const QJsonObject context{{"base_url",m_BaseUrlHttps.toString()},
        {"server_certificate",QString::fromLatin1(m_ServerCert.toPem())},
        {"client_certificate",QString::fromLatin1(IdentityManager::get()->getCertificate())},
        {"client_key",QString::fromLatin1(IdentityManager::get()->getPrivateKey())},
        {"unique_id",m_UseTrueUid?IdentityManager::get()->getUniqueId():QStringLiteral("0123456789ABCDEF")},
        {"client_name",clientName},{"nonce",QString()}};
    std::string reply;
    try { reply=SessionApiRpc::broker().call(QJsonDocument(QJsonObject{{"context",context},{"request",request}})
        .toJson(QJsonDocument::Compact).toStdString(),timeoutMs+1000); }
    catch (const SessionApiRpc::Timeout&) { throw QtNetworkReplyException(QNetworkReply::TimeoutError,tr("Host control request timed out.")); }
    catch (const std::exception&) { throw QtNetworkReplyException(QNetworkReply::OperationCanceledError,tr("Host control request cancelled or disconnected.")); }
    const auto result=QJsonDocument::fromJson(QByteArray::fromStdString(reply)).object();
    if (result["ok"].toBool() && result["value"].isObject()) return result["value"].toObject();
    const auto kind=result["kind"].toString();
    const int code=result["code"].toInt();
    auto message=result["message"].toString();
    if (kind=="host") {
        if (code==-1 && message=="Invalid") throw GfeHttpResponseException(418,tr("Missing audio capture device. Reinstalling GeForce Experience should resolve this error."));
        throw GfeHttpResponseException(code,message);
    }
    if (kind=="http") throw GfeHttpResponseException(code,tr("Host control HTTP request failed."));
    throw QtNetworkReplyException(kind=="timeout"?QNetworkReply::TimeoutError:QNetworkReply::UnknownNetworkError,
        kind=="timeout"?tr("Host control request timed out."):tr("Host control request failed."));
}
QJsonObject NvHTTP::sessionServerInfo() {
    return rustSessionCall({{"operation","info"},{"http_url",m_BaseUrlHttp.toString()}},6000);
}
QJsonObject NvHTTP::hostInformation() {
    const auto info=rustSessionCall({{"operation","host_info"},{"http_url",m_BaseUrlHttp.toString()}},30000);
    if (m_ServerCert.isNull() || httpsPort()==0) setHttpsPort(uint16_t(info["https_port"].toInt()));
    return info;
}
void NvHTTP::setRuntimeBitrate(int bitrateKbps,const QString& clientName) {
    if (usesRustSessionApi()) {rustSessionCall({{"operation","bitrate"},{"bitrate",bitrateKbps},{"client_name",clientName}},5000);return;}
    openConnectionToString(m_BaseUrlHttps,"bitrate",QString("bitrate=%1&clientname=%2").arg(bitrateKbps).arg(clientName),5000);
}

#define FAST_FAIL_TIMEOUT_MS 2000
#define REQUEST_TIMEOUT_MS 5000
#define LAUNCH_TIMEOUT_MS 120000
#define RESUME_TIMEOUT_MS 30000
#define QUIT_TIMEOUT_MS 30000

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#define XML_NAME_EQUALS(x, y) ((x) == (y))
#else
#define XML_NAME_EQUALS(x, y) ((x) == (u##y))
#endif

NvHTTP::NvHTTP(NvAddress address, uint16_t httpsPort, QSslCertificate serverCert, bool useTrueUid, QNetworkAccessManager* nam, QString uuid) :
#ifdef LITE_NATIVE_HTTP
    m_Nam(nam),
#else
    m_Nam(nam ? nam : new QNetworkAccessManager(this)),
#endif
    m_ServerCert(serverCert),
    m_UseTrueUid(useTrueUid),
    m_Uuid(uuid)
{
    m_BaseUrlHttp.setScheme("http");
    m_BaseUrlHttps.setScheme("https");

    setAddress(address);
    setHttpsPort(httpsPort);

#ifndef LITE_NATIVE_HTTP
    // Never use a proxy server
    QNetworkProxy noProxy(QNetworkProxy::NoProxy);
    m_Nam->setProxy(noProxy);
#endif
}

NvHTTP::NvHTTP(NvComputer* computer, QNetworkAccessManager* nam) :
    NvHTTP(computer->activeAddress, computer->activeHttpsPort, computer->serverCert, !computer->isNvidiaServerSoftware, nam, computer->uuid)
{
}

void NvHTTP::setServerCert(QSslCertificate serverCert)
{
    m_ServerCert = serverCert;
}

void NvHTTP::setAddress(NvAddress address)
{
    Q_ASSERT(!address.isNull());

    m_Address = address;

    m_BaseUrlHttp.setHost(address.address());
    m_BaseUrlHttps.setHost(address.address());

    m_BaseUrlHttp.setPort(address.port());
}

void NvHTTP::setHttpsPort(uint16_t port)
{
    m_BaseUrlHttps.setPort(port);
}

void NvHTTP::setTrueUid(bool useTrueUid)
{
    m_UseTrueUid = useTrueUid;
}

void NvHTTP::setHostUuid(QString uuid)
{
    m_Uuid = uuid;
}

NvAddress NvHTTP::address()
{
    return m_Address;
}

QSslCertificate NvHTTP::serverCert()
{
    return m_ServerCert;
}

uint16_t NvHTTP::httpPort()
{
    return m_BaseUrlHttp.port();
}

uint16_t NvHTTP::httpsPort()
{
    return m_BaseUrlHttps.port();
}

QVector<int>
NvHTTP::parseQuad(QString quad)
{
    QVector<int> ret;

    // Return an empty vector for old GFE versions
    // that were missing GfeVersion.
    if (quad.isEmpty()) {
        return ret;
    }

    QStringList parts = quad.split(".");
    ret.reserve(parts.length());
    for (int i = 0; i < parts.length(); i++)
    {
        ret.append(parts.at(i).toInt());
    }

    return ret;
}

int
NvHTTP::getCurrentGame(QString serverInfo)
{
    // GFE 2.8 started keeping currentgame set to the last game played. As a result, it no longer
    // has the semantics that its name would indicate. To contain the effects of this change as much
    // as possible, we'll force the current game to zero if the server isn't in a streaming session.
    QString serverState = getXmlString(serverInfo, "state");
    if (serverState.endsWith("_SERVER_BUSY"))
    {
        return getXmlString(serverInfo, "currentgame").toInt();
    }
    else
    {
        return 0;
    }
}

QString
NvHTTP::getServerInfo(NvLogLevel logLevel, bool fastFail)
{
    QString serverInfo;

    // Check if we have a pinned cert and HTTPS port for this host yet
    if (!m_ServerCert.isNull() && httpsPort() != 0)
    {
        try
        {
            // Always try HTTPS first, since it properly reports
            // pairing status (and a few other attributes).
            serverInfo = openConnectionToString(m_BaseUrlHttps,
                                                "serverinfo",
                                                nullptr,
                                                fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS,
                                                logLevel);
            // Throws if the request failed
            verifyResponseStatus(serverInfo);
        }
        catch (const GfeHttpResponseException& e)
        {
            if (e.getStatusCode() == 401)
            {
                // Only a remote status from the paired TLS peer permits this
                // recovery. Local TLS failures are QtNetworkReplyException.
                serverInfo = openConnectionToString(m_BaseUrlHttp,
                                                    "serverinfo",
                                                    nullptr,
                                                    fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS,
                                                    logLevel);
                verifyResponseStatus(serverInfo);
            }
            else
            {
                // Rethrow real errors
                throw e;
            }
        }
    }
    else
    {
        // Only use HTTP prior to pairing or fetching HTTPS port
        serverInfo = openConnectionToString(m_BaseUrlHttp,
                                            "serverinfo",
                                            nullptr,
                                            fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS,
                                            logLevel);
        verifyResponseStatus(serverInfo);

        // Populate the HTTPS port
        uint16_t httpsPort = getXmlString(serverInfo, "HttpsPort").toUShort();
        if (httpsPort == 0) {
            httpsPort = DEFAULT_HTTPS_PORT;
        }
        setHttpsPort(httpsPort);

        // If we just needed to determine the HTTPS port, we'll try again over
        // HTTPS now that we have the port number
        if (!m_ServerCert.isNull()) {
            return getServerInfo(logLevel, fastFail);
        }
    }

    return serverInfo;
}

void
NvHTTP::startApp(QString verb,
                 bool isGfe,
                 int appId,
                 PSTREAM_CONFIGURATION streamConfig,
                 bool sops,
                 bool localAudio,
                 int gamepadMask,
                 bool persistGameControllersOnDisconnect,
                 QString& rtspSessionUrl,
                 int screenCombinationMode,
                 const std::optional<bool>& useVdd,
                 const QString& displayName,
                 RemoteStreamConfig &remoteStreamConfig)
{
    int riKeyId;

    memcpy(&riKeyId, streamConfig->remoteInputAesIv, sizeof(riKeyId));
    riKeyId = qFromBigEndian(riKeyId);
    if (usesRustSessionApi()) {
        QJsonArray inputKey;
        for (unsigned char byte:streamConfig->remoteInputAesKey) inputKey.append(int(byte));
        const QJsonObject params{{"resume",verb=="resume"},{"is_gfe",isGfe},{"app_id",appId},
            {"width",streamConfig->width},{"height",streamConfig->height},{"fps",streamConfig->fps},
            {"original_width",remoteStreamConfig.originalStreamWidth},{"original_height",remoteStreamConfig.originalStreamHeight},
            {"remote_resolution",remoteStreamConfig.remoteResolution},{"remote_width",remoteStreamConfig.remoteResolutionWidth},
            {"remote_height",remoteStreamConfig.remoteResolutionHeight},{"remote_fps",remoteStreamConfig.remoteFps},
            {"remote_fps_rate",remoteStreamConfig.remoteFpsRate},{"sops",sops},{"input_key",inputKey},{"input_key_id",riKeyId},
            {"ten_bit",bool(streamConfig->supportedVideoFormats&VIDEO_FORMAT_MASK_10BIT)},{"hdr_mode",streamConfig->hdrMode},
            {"local_audio",localAudio},{"surround_audio",int(SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(streamConfig->audioConfiguration))},
            {"gamepad_mask",gamepadMask},{"persist_gamepads",persistGameControllersOnDisconnect},
            {"screen_mode",screenCombinationMode},{"use_vdd",useVdd.has_value()?QJsonValue(*useVdd):QJsonValue(QJsonValue::Null)},
            {"display",displayName},{"max_brightness",remoteStreamConfig.maxBrightness},{"min_brightness",remoteStreamConfig.minBrightness},
            {"average_brightness",remoteStreamConfig.maxAverageBrightness},{"sdr_white",remoteStreamConfig.sdrWhiteBrightness},
            {"core_query",QString::fromLatin1(LiGetLaunchUrlQueryParameters())}};
        rtspSessionUrl=rustSessionCall({{"operation","launch"},{"params",params}},LAUNCH_TIMEOUT_MS)["session_url"].toString();
        return;
    }
    // Using an FPS value over 60 causes SOPS to default to 720p60,
    // so force it to 0 to ensure the correct resolution is set. We
    // used to use 60 here but that locked the frame rate to 60 FPS
    // on GFE 3.20.3. We don't need this hack for Sunshine.
    QString appWidth = QString::number(streamConfig->width);
    QString appHeight = QString::number(streamConfig->height);
    QString appFps = QString::number((streamConfig->fps > 60 && isGfe) ? 0 : streamConfig->fps);

    // 流分辨率缩放覆盖
    if (remoteStreamConfig.originalStreamWidth > 0 && remoteStreamConfig.originalStreamHeight > 0) {
        appWidth = QString::number(remoteStreamConfig.originalStreamWidth);
        appHeight = QString::number(remoteStreamConfig.originalStreamHeight);
    }

    // 远程分辨率覆盖
    if (remoteStreamConfig.remoteResolution) {
        if (remoteStreamConfig.remoteResolutionWidth > 0) {
            appWidth = QString::number(remoteStreamConfig.remoteResolutionWidth);
        }
        if (remoteStreamConfig.remoteResolutionHeight > 0) {
            appHeight = QString::number(remoteStreamConfig.remoteResolutionHeight);
        }
    }
    // 远程帧率覆盖
    if (remoteStreamConfig.remoteFps) {
        if (remoteStreamConfig.remoteFpsRate > 0) {
            appFps = QString::number(remoteStreamConfig.remoteFpsRate);
        }
    }

    QString query =
            "appid="+QString::number(appId)+
            "&mode="+appWidth+"x"+
            appHeight+"x"+
            appFps+
            "&additionalStates=1&sops="+QString::number(sops ? 1 : 0)+
            "&rikey="+QByteArray(streamConfig->remoteInputAesKey, sizeof(streamConfig->remoteInputAesKey)).toHex()+
            "&rikeyid="+QString::number(riKeyId)+
            ((streamConfig->supportedVideoFormats & VIDEO_FORMAT_MASK_10BIT) ?
                "&hdrMode="+QString::number(streamConfig->hdrMode)+
                "&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0" :
                 "")+
            "&localAudioPlayMode="+QString::number(localAudio ? 1 : 0)+
            "&surroundAudioInfo="+QString::number(SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(streamConfig->audioConfiguration))+
            "&remoteControllersBitmap="+QString::number(gamepadMask)+
            "&gcmap="+QString::number(gamepadMask)+
            "&gcpersist="+QString::number(persistGameControllersOnDisconnect ? 1 : 0);

    if (screenCombinationMode != -1) {
        query += "&customScreenMode="+QString::number(screenCombinationMode);
    }
    if (useVdd.has_value()) {
        query += "&useVdd="+QString::number(*useVdd ? 1 : 0);
    }
    if (!displayName.isEmpty()) {
        query += "&display_name="+QString::fromLatin1(QUrl::toPercentEncoding(displayName));
    }
    if (remoteStreamConfig.maxBrightness > 0) {
        query += "&maxBrightness="+QString::number(remoteStreamConfig.maxBrightness, 'f', 3)+
                 "&minBrightness="+QString::number(remoteStreamConfig.minBrightness, 'f', 6)+
                 "&maxAverageBrightness="+QString::number(remoteStreamConfig.maxAverageBrightness, 'f', 3);
    }
    if (remoteStreamConfig.sdrWhiteBrightness >= 50.0f &&
            remoteStreamConfig.sdrWhiteBrightness <= 1000.0f) {
        // Foundation Sunshine parses this extension as an integer number of
        // nits. It uses the value to re-anchor SDR-referred content before
        // converting the host's scRGB desktop to the negotiated HDR transfer.
        query += "&sdrBrightness="+
                 QString::number(qRound(remoteStreamConfig.sdrWhiteBrightness));
    }

    query += LiGetLaunchUrlQueryParameters();

    QString response = openConnectionToString(m_BaseUrlHttps,
                                              verb,
                                              query,
                                              LAUNCH_TIMEOUT_MS);

    qInfo() << "Launch response:" << response;

    // Throws if the request failed
    verifyResponseStatus(response);

    rtspSessionUrl = getXmlString(response, "sessionUrl0");
}

void
NvHTTP::quitApp()
{
    if (usesRustSessionApi()) {
        try {rustSessionCall({{"operation","quit"},{"http_url",m_BaseUrlHttp.toString()}},45000);}
        catch (const GfeHttpResponseException& error) {
            if (error.getStatusCode()==599) throw GfeHttpResponseException(599,tr("The running game wasn't started by this PC. You must quit the game on the host PC manually or use the device that originally started the game."));
            throw;
        }
        return;
    }
    QString response =
            openConnectionToString(m_BaseUrlHttps,
                                   "cancel",
                                   nullptr,
                                   QUIT_TIMEOUT_MS);

    qInfo() << "Quit response:" << response;

    // Throws if the request failed
    verifyResponseStatus(response);

    // Newer GFE versions will just return success even if quitting fails
    // if we're not the original requester.
    if (getCurrentGame(getServerInfo(NvHTTP::NVLL_ERROR)) != 0) {
        // Generate a synthetic GfeResponseException letting the caller know
        // that they can't kill someone else's stream.
        throw GfeHttpResponseException(599, "");
    }
}

QVector<NvDisplayMode>
NvHTTP::getDisplayModeList(QString serverInfo)
{
    QXmlStreamReader xmlReader(serverInfo);
    QVector<NvDisplayMode> modes;

    while (!xmlReader.atEnd()) {
        while (xmlReader.readNextStartElement()) {
            auto name = xmlReader.name();
            if (XML_NAME_EQUALS(name, "DisplayMode")) {
                modes.append(NvDisplayMode());
            }
            else if (!modes.isEmpty()) {
                if (XML_NAME_EQUALS(name, "Width")) {
                    modes.last().width = xmlReader.readElementText().toInt();
                }
                else if (XML_NAME_EQUALS(name, "Height")) {
                    modes.last().height = xmlReader.readElementText().toInt();
                }
                else if (XML_NAME_EQUALS(name, "RefreshRate")) {
                    modes.last().refreshRate = xmlReader.readElementText().toInt();
                }
            }
        }
    }

    return modes;
}

QVector<NvApp>
NvHTTP::getAppList()
{
    QString appxml = openConnectionToString(m_BaseUrlHttps,
                                            "applist",
                                            nullptr,
                                            REQUEST_TIMEOUT_MS,
                                            NvLogLevel::NVLL_ERROR);
    verifyResponseStatus(appxml);

    QXmlStreamReader xmlReader(appxml);
    QVector<NvApp> apps;
    while (!xmlReader.atEnd()) {
        while (xmlReader.readNextStartElement()) {
            auto name = xmlReader.name();
            if (XML_NAME_EQUALS(name, "App")) {
                // We must have a valid app before advancing to the next one
                if (!apps.isEmpty() && !apps.last().isInitialized()) {
                    qWarning() << "Invalid applist XML";
                    throw std::runtime_error("Invalid applist XML");
                }
                apps.append(NvApp());
            }
            else if (!apps.isEmpty()) {
                if (XML_NAME_EQUALS(name, "AppTitle")) {
                    // If an app has no name, Sunshine may send us <AppTitle/>,
                    // which readElementText() returns as a null QString.
                    // We want to treat this as an empty QString instead, so we
                    // will explicitly convert it. An empty string will satisfy
                    // NvApp's isInitialized() check.
                    QString name = xmlReader.readElementText();
                    if (name.isNull()) {
                        name = "";
                    }
                    apps.last().name = name;
                }
                else if (XML_NAME_EQUALS(name, "ID")) {
                    apps.last().id = xmlReader.readElementText().toInt();
                }
                else if (XML_NAME_EQUALS(name, "IsHdrSupported")) {
                    apps.last().hdrSupported = xmlReader.readElementText() == "1";
                }
                else if (XML_NAME_EQUALS(name, "IsAppCollectorGame")) {
                    apps.last().isAppCollectorGame = xmlReader.readElementText() == "1";
                }
            }
        }
    }

    return apps;
}

QVariantList
NvHTTP::getDisplays()
{
    QVariantList displays;

    try {
        QString response = openConnectionToString(m_BaseUrlHttps,
                                                   "displays",
                                                   nullptr,
                                                   REQUEST_TIMEOUT_MS,
                                                   NvLogLevel::NVLL_VERBOSE);

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "Failed to parse displays JSON:" << parseError.errorString();
            return displays;
        }

        QJsonObject root = doc.object();
        int statusCode = root.value("status_code").toInt(0);
        if (statusCode != 200) {
            qWarning() << "getDisplays failed:" << root.value("status_message").toString();
            return displays;
        }

        QJsonArray displaysArray = root.value("displays").toArray();
        for (int i = 0; i < displaysArray.size(); i++) {
            QJsonObject displayObj = displaysArray[i].toObject();

            const QString displayName = displayObj.value("display_name").toString();
            QString friendlyName = displayObj.value("friendly_name").toString();
            if (friendlyName.isEmpty()) {
                friendlyName = displayName;
            }
            if (friendlyName.isEmpty()) {
                friendlyName = QString("Display %1").arg(i + 1);
            }

            QString displayTarget = displayObj.value("device_id").toString();
            if (displayTarget.isEmpty()) {
                displayTarget = displayName.isEmpty() ? friendlyName : displayName;
            }

            QVariantMap display;
            display["name"] = friendlyName;
            display["id"] = QStringLiteral("physical:%1:%2").arg(i).arg(displayTarget);
            display["target"] = displayTarget;
            display["index"] = i;
            displays.append(display);
        }
    } catch (...) {
        qWarning() << "Exception in getDisplays()";
    }

    return displays;
}

void
NvHTTP::verifyResponseStatus(QString xml)
{
    QXmlStreamReader xmlReader(xml);

    while (xmlReader.readNextStartElement())
    {
        if (XML_NAME_EQUALS(xmlReader.name(), "root"))
        {
            // Status code can be 0xFFFFFFFF in some rare cases on GFE 3.20.3, and
            // QString::toInt() will fail in that case, so use QString::toUInt()
            // and cast the result to an int instead.
            int statusCode = (int)xmlReader.attributes().value("status_code").toUInt();
            if (statusCode == 200)
            {
                // Successful
                return;
            }
            else
            {
                QString statusMessage = xmlReader.attributes().value("status_message").toString();
                if (statusCode != 401) {
                    // 401 is expected for unpaired PCs when we fetch serverinfo over HTTPS
                    qWarning() << "Request failed:" << statusCode << statusMessage;
                }
                if (statusCode == -1 && statusMessage == "Invalid") {
                    // Special case handling an audio capture error which GFE doesn't
                    // provide any useful status message for.
                    statusCode = 418;
                    statusMessage = tr("Missing audio capture device. Reinstalling GeForce Experience should resolve this error.");
                }
                throw GfeHttpResponseException(statusCode, statusMessage);
            }
        }
    }

    throw GfeHttpResponseException(-1, "Malformed XML (missing root element)");
}

QImage
NvHTTP::getBoxArt(int appId)
{
    auto reply = openConnection(m_BaseUrlHttps,
                                          "appasset",
                                          "appid="+QString::number(appId)+
                                          "&AssetType=2&AssetIdx=0",
                                          REQUEST_TIMEOUT_MS,
                                          NvLogLevel::NVLL_VERBOSE);
    QBuffer buffer(&reply.body);
    buffer.open(QIODevice::ReadOnly);
    QImage image = QImageReader(&buffer).read();

    return image;
}

QByteArray
NvHTTP::getXmlStringFromHex(QString xml,
                            QString tagName)
{
    return QByteArray::fromHex(getXmlString(xml, tagName).toUtf8());
}

QString
NvHTTP::getXmlString(QString xml,
                     QString tagName)
{
    QXmlStreamReader xmlReader(xml);

    while (!xmlReader.atEnd())
    {
        if (xmlReader.readNext() != QXmlStreamReader::StartElement)
        {
            continue;
        }

        if (xmlReader.name() == tagName)
        {
            return xmlReader.readElementText();
        }
    }

    return QString();
}

#ifndef LITE_NATIVE_HTTP
void NvHTTP::handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors)
{
    if (PairedCertificate::canIgnoreErrors(m_ServerCert, errors)) {
        reply->ignoreSslErrors(errors);
    }
}
#endif

QString
NvHTTP::openConnectionToString(QUrl baseUrl,
                               QString command,
                               QString arguments,
                               int timeoutMs,
                               NvLogLevel logLevel)
{
    auto response = openConnection(baseUrl, command, arguments, timeoutMs, logLevel);
    QBuffer buffer(&response.body);
    buffer.open(QIODevice::ReadOnly);
    QTextStream stream(&buffer);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
    return stream.readAll();
}

UsbForwarding::Capability NvHTTP::getUsbForwardingCapability()
{
    if (m_ServerCert.isNull() || httpsPort() == 0) {
        throw GfeHttpResponseException(401, "USB forwarding requires a paired host");
    }
    auto reply = openConnection(m_BaseUrlHttps,
        "api/v1/usb-forwarding", QString(), 5000, NVLL_NONE, 4096);
    // A normally trusted certificate may not emit sslErrors at all. Require
    // the paired leaf certificate even on that path before accepting credentials.
    if (!reply.paired) {
        throw QtNetworkReplyException(QNetworkReply::SslHandshakeFailedError,
                                      "USB forwarding host certificate mismatch");
    }
    if (reply.status != 200) {
        throw GfeHttpResponseException(400, "USB capability request failed");
    }
    const auto capability = UsbForwarding::Capability::parse(reply.body);
    if (!capability) throw GfeHttpResponseException(400, "Invalid USB capability response");
    return *capability;
}

bool
NvHTTP::getAbrCapabilities(int* hostMaxBitrateKbps)
{
    if (usesRustSessionApi()) {
        const auto response=rustSessionCall({{"operation","abr_capabilities"}},2000);
        if (hostMaxBitrateKbps) *hostMaxBitrateKbps=response["hostMaxBitrate"].toInt(0);
        return response["supported"].toBool(false);
    }
    QJsonObject response = openJsonConnectionToObject(m_BaseUrlHttps,
                                                      "api/abr/capabilities",
                                                      QJsonObject(),
                                                      false,
                                                      FAST_FAIL_TIMEOUT_MS,
                                                      NvLogLevel::NVLL_ERROR);
    if (hostMaxBitrateKbps != nullptr) {
        *hostMaxBitrateKbps = response.value("hostMaxBitrate").toInt(0);
    }

    return response.value("supported").toBool(false);
}

QJsonObject
NvHTTP::configureAbr(bool enabled,
                     int minBitrateKbps,
                     int maxBitrateKbps,
                     QString mode,
                     int timeoutMs)
{
    if (usesRustSessionApi()) return rustSessionCall({{"operation","abr_configure"},{"enabled",enabled},
        {"min_bitrate",minBitrateKbps},{"max_bitrate",maxBitrateKbps},{"mode",mode},{"timeout_ms",timeoutMs}},timeoutMs);
    QJsonObject body;
    body["enabled"] = enabled;
    if (enabled) {
        body["minBitrate"] = minBitrateKbps;
        body["maxBitrate"] = maxBitrateKbps;
        body["mode"] = mode;
    }

    return openJsonConnectionToObject(m_BaseUrlHttps,
                                      "api/abr",
                                      body,
                                      true,
                                      timeoutMs,
                                      NvLogLevel::NVLL_ERROR);
}

QJsonObject
NvHTTP::sendAbrFeedback(double packetLoss,
                        double rttMs,
                        double decodeFps,
                        int droppedFrames,
                        int currentBitrateKbps,
                        int timeoutMs)
{
    if (usesRustSessionApi()) return rustSessionCall({{"operation","abr_feedback"},{"packet_loss",packetLoss},
        {"rtt_ms",rttMs},{"decode_fps",decodeFps},{"dropped_frames",droppedFrames},
        {"current_bitrate",currentBitrateKbps},{"timeout_ms",timeoutMs}},timeoutMs);
    QJsonObject body;
    body["packetLoss"] = packetLoss;
    body["rttMs"] = rttMs;
    body["decodeFps"] = decodeFps;
    body["droppedFrames"] = droppedFrames;
    body["currentBitrate"] = currentBitrateKbps;

    return openJsonConnectionToObject(m_BaseUrlHttps,
                                      "api/abr/feedback",
                                      body,
                                      true,
                                      timeoutMs,
                                      NvLogLevel::NVLL_ERROR);
}

NvHTTP::HttpResponse
NvHTTP::openConnection(QUrl baseUrl,
                       QString command,
                       QString arguments,
                       int timeoutMs,
                       NvLogLevel logLevel,
                       qint64 maxResponseBytes)
{
    // Port must be set
    Q_ASSERT(baseUrl.port(0) != 0);

    // Build a URL for the request
    QUrl url(baseUrl);
    url.setPath("/" + command);

    // Get clientname - prefer pairname if available, otherwise use local hostname
    QString clientname = QHostInfo::localHostName().toUtf8();
    if (!m_Uuid.isEmpty()) {
        QString pairname = NvComputer::getPairname(m_Uuid);
        if (!pairname.isEmpty()) {
            clientname = pairname;
        }
    }

    qInfo() << "clientname:" << clientname;

    // Use a placeholder UID for GFE allow them to quit games for each other.
    url.setQuery("uniqueid=" + (m_UseTrueUid ? IdentityManager::get()->getUniqueId() : "0123456789ABCDEF") +
                 "&uuid=" + QUuid::createUuid().toRfc4122().toHex() +
                 "&clientname=" + clientname +
                 (!arguments.isNull() ? ("&" + arguments) : ""));

    return request(url, {}, false, false, timeoutMs, logLevel, maxResponseBytes);
}

NvHTTP::HttpResponse
NvHTTP::request(QUrl url, QByteArray body, bool post, bool json,
                int timeoutMs, NvLogLevel logLevel, qint64 maxResponseBytes)
{
#ifdef LITE_NATIVE_HTTP
    NativeHttp::Request native;
    native.url = url.toEncoded().toStdString();
    native.method = post ? NativeHttp::Method::Post : NativeHttp::Method::Get;
    if (json) native.contentType = "application/json";
    native.body = body.toStdString();
    native.timeoutMs = timeoutMs;
    native.maxResponseBytes = static_cast<size_t>(maxResponseBytes);
    native.allowUnpairedHttp = url.scheme() == QStringLiteral("http");
    if (!native.allowUnpairedHttp) {
        const auto pin = m_ServerCert.toDer();
        native.pinnedCertificateDer.assign(pin.begin(), pin.end());
        native.clientCertificatePem = IdentityManager::get()->getCertificate().toStdString();
        native.clientPrivateKeyPem = IdentityManager::get()->getPrivateKey().toStdString();
    }
    if (logLevel >= NVLL_VERBOSE) {
        qInfo() << "Executing native request:" << url.adjusted(QUrl::RemoveQuery | QUrl::RemoveUserInfo | QUrl::RemoveFragment).toString();
    }
    auto response = NativeHttp::performWithQtEvents(native);
    if (response.error == NativeHttp::Error::ResponseTooLarge) {
        throw GfeHttpResponseException(400, "USB capability response too large");
    }
    const auto error = NativeHttp::qtNetworkError(response);
    if (error != QNetworkReply::NoError) {
        throw QtNetworkReplyException(error, NativeHttp::qtErrorText(response));
    }
    if (response.httpStatus >= 300 && response.httpStatus < 400) {
        throw GfeHttpResponseException(static_cast<int>(response.httpStatus), "Host API redirects are not allowed");
    }
    return {QByteArray(response.body.data(), static_cast<qsizetype>(response.body.size())),
            static_cast<int>(response.httpStatus), response.certificateVerified};
#else
    QNetworkRequest request(url);

    // Host-control endpoints must never forward credentials to another URL.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    // Add our client certificate
    request.setSslConfiguration(IdentityManager::get()->getSslConfig());

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Disable HTTP/2 (GFE 3.22 doesn't like it) and Qt 6 enables it by default
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    // Use fine-grained idle timeouts to avoid calling QNetworkAccessManager::clearAccessCache(),
    // which tears down the NAM's global thread each time. We must not keep persistent connections
    // or GFE will puke.
    request.setAttribute(QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute, 0);
#endif

    auto sslErrorsConnection = connect(m_Nam, &QNetworkAccessManager::sslErrors, this, &NvHTTP::handleSslErrors);
    auto pinnedConnection = PairedCertificate::enforce(m_Nam, this, m_ServerCert);
    if (json) request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = post ? m_Nam->post(request, body) : m_Nam->get(request);

    // Run the request with a timeout if requested
    QEventLoop loop;
    bool oversized = false;
    if (maxResponseBytes > 0) {
        reply->setReadBufferSize(maxResponseBytes + 1);
        connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            if (reply->bytesAvailable() > maxResponseBytes) {
                oversized = true;
                reply->abort();
            }
        });
    }
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
    if (timeoutMs) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    }
    if (logLevel >= NvLogLevel::NVLL_VERBOSE) {
        qInfo() << "Executing request:" << url.adjusted(QUrl::RemoveQuery | QUrl::RemoveUserInfo | QUrl::RemoveFragment).toString();
    }
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    // Abort the request if it timed out
    if (!reply->isFinished())
    {
        if (logLevel >= NvLogLevel::NVLL_ERROR) {
            qWarning() << "Aborting timed out request for" << url.adjusted(QUrl::RemoveQuery | QUrl::RemoveUserInfo | QUrl::RemoveFragment).toString();
        }
        reply->abort();
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    // If we couldn't use fine-grained connection idle timeouts, kill them all now
    m_Nam->clearAccessCache();
#endif
    disconnect(sslErrorsConnection);
    disconnect(pinnedConnection);
    if (url.scheme() == QStringLiteral("https") &&
        (reply->error() == QNetworkReply::NoError ||
         !reply->sslConfiguration().peerCertificate().isNull()) &&
        !PairedCertificate::matches(m_ServerCert, reply->sslConfiguration().peerCertificate())) {
        delete reply;
        throw QtNetworkReplyException(QNetworkReply::SslHandshakeFailedError,
                                      "Server certificate mismatch");
    }

    // Handle error
    if (oversized) {
        delete reply;
        throw GfeHttpResponseException(400, "USB capability response too large");
    }
    if (reply->error() != QNetworkReply::NoError)
    {
        if (logLevel >= NvLogLevel::NVLL_ERROR) {
            qWarning() << "Host request failed with error:" << reply->error();
        }

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            QtNetworkReplyException exception(QNetworkReply::TimeoutError, "Request timed out");
            delete reply;
            throw exception;
        } else {
            QtNetworkReplyException exception(reply->error(), reply->errorString());
            delete reply;
            throw exception;
        }
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus >= 300 && httpStatus < 400) {
        delete reply;
        throw GfeHttpResponseException(httpStatus, "Host API redirects are not allowed");
    }
    HttpResponse response{reply->readAll(), httpStatus,
        PairedCertificate::matches(m_ServerCert, reply->sslConfiguration().peerCertificate())};
    delete reply;
    return response;
#endif
}

QJsonObject
NvHTTP::openJsonConnectionToObject(QUrl baseUrl,
                                   QString command,
                                   QJsonObject body,
                                   bool post,
                                   int timeoutMs,
                                   NvLogLevel logLevel)
{
    const auto reply = openJsonConnection(baseUrl, command, body, post, timeoutMs, logLevel);
    const auto& response = reply.body;

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << command << "JSON response parse failed:" << parseError.errorString();
        throw GfeHttpResponseException(-1, "Malformed JSON response");
    }

    return document.object();
}

NvHTTP::HttpResponse
NvHTTP::openJsonConnection(QUrl baseUrl,
                           QString command,
                           QJsonObject body,
                           bool post,
                           int timeoutMs,
                           NvLogLevel logLevel)
{
    // Port must be set
    Q_ASSERT(baseUrl.port(0) != 0);

    QUrl url(baseUrl);
    url.setPath("/" + command);

    return request(url, QJsonDocument(body).toJson(QJsonDocument::Compact), post, true,
                   timeoutMs, logLevel);
}
