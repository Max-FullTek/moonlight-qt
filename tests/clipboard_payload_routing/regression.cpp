#include "streaming/clipboardsync.h"
#include "streaming/clipboardipc.h"

#include <QBuffer>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QtEndian>

#ifdef LITE_NATIVE_CLIPBOARD_HTTP
#include "streaming/windowsclipboardlistener.h"
#include "streaming/windowsclipboardpng.h"
#include <windows.h>
#include <cstring>
#endif

namespace {
bool check(bool value, const char* name, QTextStream& err)
{
    if (!value)
        err << "FAIL: " << name << '\n';
    return value;
}
QByteArray fixture(const char* name)
{
    QFile file(QStringLiteral(":/clipboard-test/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}
QByteArray wire(int kind, quint32 token, const QByteArray& payload)
{
    QByteArray result;
    result.append(char(1));
    result.append(char(kind));
    const quint32 t = qToLittleEndian(token), size = qToLittleEndian(quint32(payload.size()));
    result.append(reinterpret_cast<const char*>(&t), 4);
    result.append(reinterpret_cast<const char*>(&size), 4);
    return result + payload;
}
void incoming(ClipboardSync& sync, int kind, quint32 token, const QByteArray& payload)
{
    QMetaObject::invokeMethod(&sync, "onIncomingFrame", Qt::DirectConnection,
                              Q_ARG(QByteArray, wire(kind, token, payload)));
}
QByteArray ref(const char* id, int size)
{
    return QJsonDocument(QJsonObject{ { "id", QString::fromLatin1(id) },
                                      { "mime", "text/plain" },
                                      { "size", size } })
        .toJson(QJsonDocument::Compact);
}
bool waitFor(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return condition();
}

// Only loopback test traffic. The checked-in key is a public test fixture.
class BlobServer : public QTcpServer
{
public:
    QSslCertificate certificate{ fixture("server.pem") };
    QSslKey key{ fixture("server-key.pem"), QSsl::Rsa };
    QList<QPointer<QSslSocket>> requests;
    void answer(int index, const QByteArray& body, int declaredSize = -1)
    {
        if (index >= requests.size() || requests[index].isNull() ||
            requests[index]->state() != QAbstractSocket::ConnectedState)
            return;
        auto socket = requests[index];
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                      QByteArray::number(declaredSize < 0 ? body.size() : declaredSize) +
                      "\r\n\r\n" + body);
        socket->flush();
    }
    void answerChunked(int index, const QByteArray& body)
    {
        if (index >= requests.size() || requests[index].isNull())
            return;
        requests[index]->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" +
                               QByteArray::number(body.size(), 16) + "\r\n" + body +
                               "\r\n0\r\n\r\n");
        requests[index]->flush();
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto* socket = new QSslSocket(this);
        socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(certificate);
        socket->setPrivateKey(key);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QSslSocket::readyRead, this, [this, socket, buffer]() {
            *buffer += socket->readAll();
            const int end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            int contentLength = 0;
            for (auto line : buffer->left(end).split('\n')) {
                if (line.toLower().startsWith("content-length:")) {
                    contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                }
            }
            if (buffer->size() < end + 4 + contentLength)
                return;
            buffer->remove(0, end + 4 + contentLength);
            requests.append(socket);
        });
        socket->startServerEncryption();
    }
};
}

class ClipboardSyncTest
{
public:
    static bool run(QTextStream& err)
    {
        bool ok = true;
        auto* cb = QGuiApplication::clipboard();
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
#ifdef LITE_NATIVE_CLIPBOARD_HTTP
        {
            ClipboardIpc::Message message;
            QString error;
            const QByteArray bytes("a\x00\xff", 3);
            ok &= check(ClipboardIpc::decodeLine(ClipboardIpc::encodeHostFrame(0xffffffff, bytes), message, error) &&
                            message.type == ClipboardIpc::MessageType::HostFrame &&
                            message.sequence == 0xffffffff && message.frame == bytes,
                        "native clipboard IPC preserves binary payload and uint32 sequence", err);
            ok &= check(!ClipboardIpc::decodeLine(
                            R"({"version":1,"type":"hostFrame","sequence":1,"payload":"!!!"})", message, error),
                        "native clipboard IPC rejects malformed base64", err);
            const QByteArray deeplyNested = R"({"version":1,"type":"ping","sequence":1,"extra":)" +
                QByteArray(130, '[') + "0" + QByteArray(130, ']') + "}";
            ok &= check(!ClipboardIpc::decodeLine(deeplyNested, message, error),
                        "native clipboard IPC bounds JSON depth", err);
        }
        {
            BITMAPINFOHEADER header{};
            header.biSize = sizeof(header);
            header.biWidth = 1;
            header.biHeight = 1;
            header.biPlanes = 1;
            header.biBitCount = 24;
            header.biCompression = BI_RGB;
            const char pixels[] = { 0x33, 0x22, 0x11, 0 };
            std::string dib(reinterpret_cast<const char*>(&header), sizeof(header));
            dib.append(pixels, sizeof(pixels));
            const auto decodeBitmap = [](std::string_view input) {
                const auto bmp = WindowsClipboardBitmap::toBmp(input, 4096);
                return QImage::fromData(
                    QByteArray::fromRawData(bmp.data(), static_cast<qsizetype>(bmp.size())), "BMP");
            };
            const auto opaque = decodeBitmap(dib);
            ok &= check(opaque.size() == QSize(1, 1) && opaque.pixel(0, 0) == qRgb(0x11, 0x22, 0x33),
                        "packed DIB decodes as an opaque bitmap", err);
            ok &= check(WindowsClipboardBitmap::toBmp(dib, dib.size()).empty() &&
                        WindowsClipboardBitmap::toBmp(std::string_view(dib).substr(0, dib.size() - 1), 4096).empty(),
                        "packed DIB must fit the transfer and contain a complete row", err);

            header.biBitCount = 8;
            header.biClrUsed = 2;
            const RGBQUAD palette[] = { {}, { 0x33, 0x22, 0x11, 0 } };
            std::string indexed(reinterpret_cast<const char*>(&header), sizeof(header));
            indexed.append(reinterpret_cast<const char*>(palette), sizeof(palette));
            indexed.append("\x01\x00\x00\x00", 4);
            const auto indexedBitmap = decodeBitmap(indexed);
            ok &= check(indexedBitmap.size() == QSize(1, 1) &&
                            indexedBitmap.pixel(0, 0) == qRgb(0x11, 0x22, 0x33),
                        "packed indexed DIB reads pixels after the color table", err);

            BITMAPV5HEADER v5{};
            v5.bV5Size = sizeof(v5);
            v5.bV5Width = 1;
            v5.bV5Height = -1;
            v5.bV5Planes = 1;
            v5.bV5BitCount = 32;
            v5.bV5Compression = BI_BITFIELDS;
            v5.bV5RedMask = 0x00ff0000;
            v5.bV5GreenMask = 0x0000ff00;
            v5.bV5BlueMask = 0x000000ff;
            v5.bV5AlphaMask = 0xff000000;
            v5.bV5CSType = LCS_sRGB;
            const char alphaPixels[] = { 0x33, 0x22, 0x11, 0x40 };
            std::string dibV5(reinterpret_cast<const char*>(&v5), sizeof(v5));
            dibV5.append(alphaPixels, sizeof(alphaPixels));
            const auto transparent = decodeBitmap(dibV5);
            ok &= check(transparent.size() == QSize(1, 1) && transparent.pixel(0, 0) == qRgba(0x11, 0x22, 0x33, 0x40),
                        "packed DIBV5 preserves explicit alpha", err);

            for (const auto& sample : {std::pair{&dib, 0xff}, std::pair{&indexed, 0xff},
                                       std::pair{&dibV5, 0x40}}) {
                const auto bmp = WindowsClipboardBitmap::toBmp(*sample.first, 4096);
                const std::string_view bytes(bmp.data(), bmp.size());
                const auto converted = WindowsClipboardPng::convertBitmap(bytes, 1, 4096);
                const auto decoded = WindowsClipboardPng::decode(
                    {converted.png.data(), converted.png.size()}, 1);
                const std::vector<unsigned char> expected{0x11, 0x22, 0x33,
                                                          static_cast<unsigned char>(sample.second)};
                ok &= check(converted.image.rgba == expected && decoded.rgba == expected,
                            "WIC converts packed DIB to PNG without changing pixels or alpha", err);
                ok &= check(WindowsClipboardPng::convertBitmap(bytes, 0, 4096).png.empty() &&
                            WindowsClipboardPng::convertBitmap(bytes, 1, 16).png.empty(),
                            "WIC conversion respects pixel and output limits", err);
            }

            v5.bV5CSType = PROFILE_LINKED;
            v5.bV5ProfileData = static_cast<DWORD>(dibV5.size());
            v5.bV5ProfileSize = 8;
            std::memcpy(dibV5.data(), &v5, sizeof(v5));
            dibV5.append("profile", 8);
            const auto normalized = WindowsClipboardBitmap::toBmp(dibV5, 4096);
            BITMAPV5HEADER normalizedHeader{};
            if (normalized.size() >= sizeof(BITMAPFILEHEADER) + sizeof(normalizedHeader)) {
                std::memcpy(&normalizedHeader, normalized.data() + sizeof(BITMAPFILEHEADER),
                            sizeof(normalizedHeader));
            }
            ok &= check(normalizedHeader.bV5CSType == LCS_sRGB && !normalizedHeader.bV5ProfileData &&
                            !normalizedHeader.bV5ProfileSize,
                        "packed DIBV5 never dereferences a linked profile path", err);
        }
        {
            cb->setText("keep clipboard");
            ClipboardSync sync;
            int notices = 0;
            QObject::connect(&sync, &ClipboardSync::unsupportedFormat, [&] { ++notices; });
            sync.start();
            incoming(sync, 2, 0, "invalid PNG");
            incoming(sync, 3, 0, R"({"id":"unsupported","mime":"image/jpeg","size":4})");
            ok &= check(notices == 2 && cb->text() == "keep clipboard",
                        "unsupported inbound images raise a notice and preserve the clipboard", err);
        }
#endif
#ifndef Q_OS_MACOS
        {
            cb->setText("baseline");
            ClipboardSync sync;
            sync.start();
            incoming(sync, 1, 42, "remote-text");
            ok &= check(cb->text() == "remote-text", "nonzero token text applies immediately", err);
            incoming(sync, 2, 42, png);
            ok &= check(cb->text() == "remote-text" && cb->mimeData()->hasImage(),
                        "compound inbound retains both flavors", err);
            incoming(sync, 1, 0, "newer");
            incoming(sync, 2, 42, png);
            ok &= check(cb->text() == "newer" && !cb->mimeData()->hasImage(),
                        "retired burst cannot overwrite newer clipboard", err);
            incoming(sync, 2, 43, png);
            incoming(sync, 1, 43, "late-text");
            ok &= check(cb->text() == "late-text" && cb->mimeData()->hasImage(),
                        "reverse flavor order aggregates", err);
        }
        {
            cb->clear();
            ClipboardSync sync;
            QList<QByteArray> sent;
            QObject::connect(&sync, &ClipboardSync::outboundFrame,
                             [&](QByteArray b) { sent.append(b); });
            sync.start();
            cb->setText("A");
            cb->setText("B");
            cb->setText("A");
            ok &= check(sent.size() == 3, "A B A must send three updates", err);
            QMetaObject::invokeMethod(&sync, "onLocalClipboardChanged", Qt::DirectConnection);
            ok &= check(sent.size() == 3, "duplicate notification suppressed", err);
            incoming(sync, 1, 0, "host");
            QMetaObject::invokeMethod(&sync, "onLocalClipboardChanged", Qt::DirectConnection);
            ok &= check(sent.size() == 3, "inbound clipboard does not echo", err);
        }
        {
            cb->clear();
            ClipboardSync sync;
            QList<QByteArray> sent;
            QObject::connect(&sync, &ClipboardSync::outboundFrame,
                             [&](QByteArray b) { sent.append(b); });
            sync.start();
            for (const auto& text :
                 { QByteArray("caption1"), QByteArray("caption2"), QByteArray(60000, 'x') }) {
                auto* mime = new QMimeData;
                mime->setText(QString::fromUtf8(text));
                mime->setData("image/png", png);
                cb->setMimeData(mime);
            }
            ok &=
                check(sent.size() == 6 && sent[4][1] == 1 && sent[5][1] == 2,
                      "same image new caption and 60000 byte text preserve full inline burst", err);
            if (sent.size() == 6) {
                const auto token = qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar*>(sent[4].constData() + 2));
                incoming(sync, 1, token, QByteArray(60000, 'x'));
                ok &= check(cb->mimeData()->data("image/png") == png,
                            "echoed compound text must not replace local image", err);
            }
        }
        {
            QTemporaryDir dir;
            const QString path = dir.filePath("private.png");
            image.save(path);
            cb->clear();
            ClipboardSync sync;
            QList<QByteArray> sent;
            QObject::connect(&sync, &ClipboardSync::outboundFrame,
                             [&](QByteArray b) { sent.append(b); });
            sync.start();
            auto* file = new QMimeData;
            file->setUrls({ QUrl::fromLocalFile(path) });
            cb->setMimeData(file);
            ok &= check(sent.isEmpty(), "file URL cannot upload image file", err);
            auto* html = new QMimeData;
            html->setHtml("<img src='" + QUrl::fromLocalFile(path).toString() + "'>");
            cb->setMimeData(html);
            ok &= check(sent.isEmpty(), "HTML cannot read local image file", err);
            auto* chat = new QMimeData;
            chat->setUrls({ QUrl::fromLocalFile(path) });
            chat->setData("image/png", png);
            chat->setText(path);
            cb->setMimeData(chat);
            ok &= check(sent.size() == 1 && sent[0][1] == 2,
                        "attached chat bitmap syncs without leaking path", err);
            auto* shell = new QMimeData;
            shell->setData("Shell IDList Array", "file");
            shell->setData("image/png", png);
            cb->setMimeData(shell);
            ok &= check(sent.size() == 1, "shell icon cannot bypass file protection", err);
        }
        {
            // A BMP header with excessive dimensions must fail before pixel allocation.
            QByteArray bmp(54, '\0');
            bmp[0] = 'B';
            bmp[1] = 'M';
            qToLittleEndian<quint32>(54, reinterpret_cast<uchar*>(bmp.data() + 2));
            qToLittleEndian<quint32>(54, reinterpret_cast<uchar*>(bmp.data() + 10));
            qToLittleEndian<quint32>(40, reinterpret_cast<uchar*>(bmp.data() + 14));
            qToLittleEndian<quint32>(100000, reinterpret_cast<uchar*>(bmp.data() + 18));
            qToLittleEndian<quint32>(100000, reinterpret_cast<uchar*>(bmp.data() + 22));
            bmp[26] = 1;
            bmp[28] = 24;
            QImage decoded;
            ok &= check(!ClipboardSync::decodeImage(bmp, decoded),
                        "oversized image header rejected", err);
        }
#endif
        {
            ClipboardSync sync;
            QByteArray frame;
            ok &= check(
                sync.encodeFrame(1, 1, QByteArray(ClipboardSync::MAX_INLINE_PAYLOAD, 'x'), frame) &&
                    frame.size() + 24 <= 65535,
                "inline frame fits encrypted control length", err);
            ok &= check(!sync.encodeFrame(
                            1, 1, QByteArray(ClipboardSync::MAX_INLINE_PAYLOAD + 1, 'x'), frame),
                        "inline frame cannot overflow encrypted control length", err);
        }
        {
            QByteArray aggregate;
            const QByteArray line = ClipboardIpc::encodeLocalFrame(1, QByteArray(60000, 'x'));
            for (int i = 0; i < 20; ++i)
                aggregate += line + '\n';
            QString error;
            QByteArray next;
            int lines = 0;
            while (ClipboardIpc::takeLine(aggregate, next, error))
                ++lines;
            ok &= check(lines == 20 && error.isEmpty(),
                        "multiple legal IPC lines may exceed one MiB in aggregate", err);
            aggregate.fill('x', ClipboardIpc::MAX_LINE_BYTES + 1);
            ok &= check(!ClipboardIpc::takeLine(aggregate, next, error) && !error.isEmpty(),
                        "oversized partial IPC line rejected", err);
            ClipboardIpc::Message message;
            ok &=
                check(ClipboardIpc::decodeLine(ClipboardIpc::encodePong(99), message, error) &&
                          message.type == ClipboardIpc::MessageType::Pong && message.sequence == 99,
                      "heartbeat protocol roundtrip", err);
        }
        BlobServer server;
        if (!check(QSslSocket::supportsSsl() && !server.certificate.isNull() &&
                       !server.key.isNull() && server.listen(QHostAddress::LocalHost),
                   "loopback TLS fixture starts", err))
            return false;
        ClipboardSyncHostContext host;
        host.address = "127.0.0.1";
        host.httpsPort = server.serverPort();
#ifdef LITE_NATIVE_CLIPBOARD_HTTP
        auto credentials = NativeHttp::parseClipboardCredentials(
            fixture("server.pem").toStdString(), fixture("server.pem").toStdString(),
            fixture("server-key.pem").toStdString());
        if (!check(credentials.has_value(), "parse native clipboard identity", err)) return false;
        host.credentials = std::move(*credentials);
#else
        host.serverCertificate = server.certificate;
        host.clientCertificate = server.certificate;
        host.clientPrivateKey = QSslKey(fixture("server-key.pem"), QSsl::Rsa);
#endif
        {
            cb->setText("network baseline");
            ClipboardSync sync(host);
            sync.start();
            incoming(sync, 3, 0, ref("first", 6));
            ok &= check(waitFor([&] { return server.requests.size() == 1; }),
                        "paired TLS GET reaches server", err);
            server.answer(0, "remote");
            ok &= check(waitFor([&] { return cb->text() == "remote"; }), "valid blob applied", err);
            incoming(sync, 3, 0, ref("stale", 3));
            ok &= check(waitFor([&] { return server.requests.size() == 2; }),
                        "pending stale GET started", err);
            incoming(sync, 1, 0, "newer text");
            server.answer(1, "old");
            QCoreApplication::processEvents();
            ok &= check(sync.m_BlobReplies.empty() && cb->text() == "newer text",
                        "new frame cancels stale download", err);
            incoming(sync, 3, 0, ref("oversize", 4));
            ok &= check(waitFor([&] { return server.requests.size() == 3; }), "limit GET started",
                        err);
            server.answer(2, "", 100000);
            ok &= check(waitFor([&] { return sync.m_BlobReplies.empty(); }) &&
                            cb->text() == "newer text",
                        "oversize response aborted at headers", err);
            incoming(sync, 3, 0, ref("stop", 3));
            ok &= check(waitFor([&] { return server.requests.size() == 4; }), "stop GET started",
                        err);
            sync.stop();
            server.answer(3, "old");
            QCoreApplication::processEvents();
            ok &= check(sync.m_BlobReplies.empty() && cb->text() == "newer text",
                        "stop cancels pending network callbacks", err);
        }
        {
            cb->clear();
            ClipboardSync sync(host);
            QList<QByteArray> sent;
            QObject::connect(&sync, &ClipboardSync::outboundFrame,
                             [&](QByteArray b) { sent.append(b); });
            sync.start();
            const int index = server.requests.size();
            cb->setText(QString(60000, 'x'));
            ok &= check(waitFor([&] { return server.requests.size() > index; }),
                        "large clipboard POST started", err);
            cb->setText("new upload");
            server.answer(index, "{\"id\":\"late\"}");
            QCoreApplication::processEvents();
            ok &= check(sent.size() == 1 && sent[0][1] == 1 && sync.m_BlobReplies.empty(),
                        "new clipboard cancels stale upload REF", err);
        }
        {
            cb->setText("keep");
            ClipboardSync sync(host);
            sync.start();
            int index = server.requests.size();
            incoming(sync, 3, 0, ref("chunked", 4));
            ok &= check(waitFor([&] { return server.requests.size() > index; }),
                        "chunked GET started", err);
            incoming(sync, 3, 0, "invalid JSON");
            ok &= check(!sync.m_BlobReplies.empty(),
                        "malformed REF does not cancel valid transfer", err);
            server.answerChunked(index, "12345678");
            ok &=
                check(waitFor([&] { return sync.m_BlobReplies.empty(); }) && cb->text() == "keep",
                      "streamed body is bounded without Content-Length", err);
            index = server.requests.size();
            incoming(sync, 3, 0, ref("reconfigure", 3));
            ok &= check(waitFor([&] { return server.requests.size() > index; }),
                        "reconfigure GET started", err);
            sync.setHostContext(host);
            server.answer(index, "old");
            QCoreApplication::processEvents();
            ok &= check(sync.m_BlobReplies.empty() && cb->text() == "keep",
                        "reconfigure same host cancels prior generation", err);
        }
        {
            ClipboardSync sync(host);
            sync.start();
            const int index = server.requests.size();
            QList<QByteArray> completed;
            for (int i = 0; i <= ClipboardBlob::MaxPendingTransfers; ++i) {
                sync.fetchBlob(QStringLiteral("concurrent-%1").arg(i), 1,
                               [&](const QByteArray& bytes) { completed.append(bytes); });
            }
            ok &= check(waitFor([&] { return server.requests.size() == index + ClipboardBlob::MaxPendingTransfers; }),
                        "four clipboard GETs reach the server before any completes", err);
            for (int i = ClipboardBlob::MaxPendingTransfers - 1; i >= 0; --i) {
                server.answer(index + i, "x");
            }
            ok &= check(waitFor([&] { return sync.m_BlobReplies.empty(); }) &&
                        completed.size() == ClipboardBlob::MaxPendingTransfers &&
                        server.requests.size() == index + ClipboardBlob::MaxPendingTransfers,
                        "concurrent clipboard completions respect the four-request cap", err);
        }
        {
            const QSslConfiguration previous = QSslConfiguration::defaultConfiguration();
            auto trusted = previous;
            trusted.setCaCertificates({ server.certificate });
            QSslConfiguration::setDefaultConfiguration(trusted);
            auto wrongHost = host;
#ifdef LITE_NATIVE_CLIPBOARD_HTTP
            auto other = NativeHttp::parseClipboardCredentials(
                fixture("other.pem").toStdString(), fixture("server.pem").toStdString(),
                fixture("server-key.pem").toStdString());
            if (!check(other.has_value(), "parse alternate native clipboard pin", err)) return false;
            wrongHost.credentials = std::move(*other);
#else
            wrongHost.serverCertificate = QSslCertificate(fixture("other.pem"));
#endif
            ClipboardSync sync(wrongHost);
            sync.start();
#ifndef LITE_NATIVE_CLIPBOARD_HTTP
            int sslErrors = 0, encrypted = 0;
            QObject::connect(sync.nam(), &QNetworkAccessManager::sslErrors, [&] { ++sslErrors; });
            QObject::connect(sync.nam(), &QNetworkAccessManager::encrypted, [&] { ++encrypted; });
#endif
            const int count = server.requests.size();
            cb->setText(QString(60000, 'z'));
            ok &= check(waitFor([&] { return sync.m_BlobReplies.empty(); }),
                        "wrong paired certificate rejected", err);
#ifndef LITE_NATIVE_CLIPBOARD_HTTP
            ok &= check(sslErrors == 0 && encrypted == 1,
                        "Qt transport reaches the normally trusted TLS callback", err);
#endif
            ok &= check(server.requests.size() == count,
                        "trusted but unpaired certificate receives no HTTP clipboard body", err);
            QSslConfiguration::setDefaultConfiguration(previous);
        }
        return ok;
    }
};

bool runClipboardRegression(QTextStream& err)
{
    return ClipboardSyncTest::run(err);
}
