#define SDL_MAIN_HANDLED
#include "streaming/clipboardhelperclient.h"
#include "streaming/clipboardipc.h"
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
#include "streaming/windowsclipboardprocess.h"
#endif
#include <QCoreApplication>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <iostream>
#include <vector>

// A stuck process API must fail the regression instead of occupying a CI runner
// indefinitely. This watchdog does not depend on the Qt event loop under test.
class TestDeadline
{
public:
    TestDeadline()
        : worker([this] {
              std::unique_lock<std::mutex> lock(mutex);
              if (!finished.wait_for(lock, std::chrono::seconds(60), [this] { return done; })) {
                  std::fputs("FAIL: clipboard helper lifecycle exceeded 60 seconds\n", stderr);
                  std::fflush(stderr);
                  std::_Exit(EXIT_FAILURE);
              }
          })
    {
    }
    ~TestDeadline()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        finished.notify_one();
        worker.join();
    }

private:
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false;
    std::thread worker;
};

static std::vector<std::string> sentFrames;
extern "C" int LiSendClipboardData(const void* data, int length)
{
    sentFrames.emplace_back(static_cast<const char*>(data), static_cast<size_t>(length));
    return 0;
}

static bool checkPackagedHelper(const QString& helper, const QDir& fixtures, QTextStream& err)
{
    QFile cert(fixtures.filePath(QStringLiteral("server.pem")));
    QFile key(fixtures.filePath(QStringLiteral("server-key.pem")));
    if (!cert.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly)) {
        err << "FAIL: cannot read the public helper IPC fixtures\n";
        return false;
    }
    const auto certificate = cert.readAll();
    const ClipboardIpc::HostConfig host{QStringLiteral("127.0.0.1"), 47984,
                                        certificate, certificate, key.readAll()};
    const auto input = ClipboardIpc::encodeHostFrame(1, QByteArray("a\x00\xff", 3)) + '\n' +
        ClipboardIpc::encodeConfigure(2, host) + '\n' + ClipboardIpc::encodePing(3) + '\n' +
        ClipboardIpc::encodeStop(4) + '\n';
    QProcess process;
    process.start(helper, QStringList{});
    if (!process.waitForStarted(3000) || process.write(input) != input.size()) {
        err << "FAIL: cannot start or write to the packaged clipboard helper\n";
        return false;
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0) {
        err << "FAIL: packaged clipboard helper did not exit normally\n";
        return false;
    }
    auto output = process.readAllStandardOutput();
    QByteArray line;
    QString error;
    ClipboardIpc::Message message;
    const auto next = [&](ClipboardIpc::MessageType type, quint32 sequence) {
        return ClipboardIpc::takeLine(output, line, error) && ClipboardIpc::decodeLine(line, message, error) &&
            message.type == type && message.sequence == sequence;
    };
    using Type = ClipboardIpc::MessageType;
    if (!next(Type::Error, 1) || message.code != QStringLiteral("not-ready") ||
        !next(Type::Ready, 2) || !next(Type::Pong, 3) || !output.isEmpty()) {
        err << "FAIL: packaged helper messages did not match the Qt worker's IPC contract\n";
        return false;
    }
    return true;
}

class ClipboardHelperClientTest
{
public:
    static bool start(ClipboardHelperClient& client, const QString& mode)
    {
        std::fprintf(stderr, "Starting lifecycle case: %s\n", qPrintable(mode));
        std::fflush(stderr);
        client.m_Enabled = true;
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
        client.m_Process = std::make_unique<WindowsClipboardProcess>();
        const auto oldMode = qgetenv("MOONLIGHT_CLIPBOARD_CLIENT_TEST_CHILD");
        qputenv("MOONLIGHT_CLIPBOARD_CLIENT_TEST_CHILD", mode.toUtf8());
        const bool started = client.m_Process->start(QCoreApplication::applicationFilePath().toStdWString());
        if (oldMode.isNull()) qunsetenv("MOONLIGHT_CLIPBOARD_CLIENT_TEST_CHILD");
        else qputenv("MOONLIGHT_CLIPBOARD_CLIENT_TEST_CHILD", oldMode);
        client.m_LastPingTicks = client.m_LastResponseTicks = SDL_GetTicks();
        return started;
#else
        client.m_Process = std::make_unique<QProcess>();
        client.m_Process->start(QCoreApplication::applicationFilePath(), { mode });
        std::fputs("Helper process launched; initializing timer\n", stderr);
        std::fflush(stderr);
        client.m_LastPingTicks = client.m_LastResponseTicks = SDL_GetTicks();
        return client.m_Process->waitForStarted(3000);
#endif
    }
    static bool run(QTextStream& err)
    {
        bool ok = true;
        const auto check = [&](bool value, const char* message) {
            if (!value) {
                err << "FAIL: " << message << '\n';
                ok = false;
            }
        };
        {
            int notices = 0;
            ClipboardHelperClient client({}, [&] { ++notices; });
            const auto unsupported = ClipboardIpc::encodeError(
                0, QStringLiteral("unsupported-format"), QString());
            client.processProtocolLine(unsupported.toStdString());
            check(notices == 0, "unconfigured helper cannot raise a format notice");
            client.m_HelperReady = true;
            client.processProtocolLine(ClipboardIpc::encodeError(
                0, QStringLiteral("bad-message"), QString()).toStdString());
            client.processProtocolLine(unsupported.toStdString());
            check(notices == 1, "only unsupported format raises a format notice");
            check(client.m_HelperReady, "unsupported format does not reset the helper");
        }
        {
            ClipboardHelperClient client([] {
                return NativeClipboardIpc::Host{"localhost", 47984, "server", "client", "key"};
            });
            // The receive buffer belongs to the producer and may be reused as
            // soon as handleIncomingFrame returns. Include NUL and non-UTF-8.
            std::vector<std::string> expected;
            for (int i = 8; i < 40; ++i) {
                expected.push_back(std::string{static_cast<char>(i), '\0', '\xff', 'x'});
            }
            std::thread receiver([&] {
                char frame[4]{};
                for (int i = 0; i < 40; ++i) {
                    frame[0] = static_cast<char>(i);
                    frame[1] = '\0'; frame[2] = '\xff'; frame[3] = 'x';
                    client.handleIncomingFrame(frame, sizeof(frame));
                    std::memset(frame, 0, sizeof(frame));
                }
                client.handleIncomingFrame(nullptr, 1);
                client.handleIncomingFrame(frame, 0);
                client.handleIncomingFrame(frame, -1);
            });
            receiver.join();
            check(client.m_DroppedInboundFrames == 8, "bounded receive queue drops eight oldest frames");
            if (!start(client, "--reflect-frames")) return false;
            check(client.sendCurrentConfig(), "send helper host configuration");
            sentFrames.clear();
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (sentFrames.size() < expected.size() && std::chrono::steady_clock::now() < until) {
                if (!client.m_Process) break;
                client.processPendingMessages();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            check(client.m_HelperReady, "helper acknowledges the matching host configuration");
            check(sentFrames == expected, "cross-thread frames preserve binary content and FIFO order through helper IPC");
            check(client.m_InboundFrames.empty() && client.m_DroppedInboundFrames == 0,
                  "flushing consumes queued frames and drop count");
        }
#if defined(_WIN32) && defined(NDEBUG)
        {
            QTemporaryDir untrusted;
            check(untrusted.isValid(), "isolated untrusted helper directory");
            if (!untrusted.isValid()) return false;
            const QString previousDirectory = QDir::currentPath();
            const QByteArray previousOverride = qgetenv("MOONLIGHT_CLIPBOARD_HELPER");
            const bool hadOverride = qEnvironmentVariableIsSet("MOONLIGHT_CLIPBOARD_HELPER");
            const QString fakePath = untrusted.filePath("moonlight-clipboard-helper.exe");
            QFile fake(fakePath);
            check(fake.open(QIODevice::WriteOnly), "create harmless helper stand-in");
            fake.close();
            qputenv("MOONLIGHT_CLIPBOARD_HELPER", fakePath.toUtf8());
            check(QDir::setCurrent(untrusted.path()), "enter untrusted working directory");
            ClipboardHelperClient client({});
            const QString expected = QDir(QCoreApplication::applicationDirPath())
                .filePath("moonlight-clipboard-helper.exe");
            const QString resolved = QString::fromStdU16String(client.findHelperExecutable().u16string());
            check(resolved != fakePath, "release helper ignores environment and working directory");
            check(resolved == (QFileInfo(expected).isFile() ? expected : QString()),
                  "release helper uses bundled executable or disables clipboard");
            QDir::setCurrent(previousDirectory);
            if (hadOverride) qputenv("MOONLIGHT_CLIPBOARD_HELPER", previousOverride);
            else qunsetenv("MOONLIGHT_CLIPBOARD_HELPER");
        }
#endif
        {
            ClipboardHelperClient client({});
            if (!start(client, "--invalid-output"))
                return false;
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
            check(client.m_Process->wait(3000), "fake helper exits");
#else
            check(client.m_Process->waitForFinished(3000), "fake helper exits");
#endif
            client.processPendingMessages();
            check(client.m_Process == nullptr && client.m_RestartAttempts == 1,
                  "invalid trailing output schedules one restart without null dereference");
        }
        {
            ClipboardHelperClient client({});
            if (!start(client, "--hung"))
                return false;
            client.m_LastResponseTicks = SDL_GetTicks() - 30001;
            client.processPendingMessages();
            check(client.m_Process == nullptr && client.m_RestartAttempts == 1,
                  "living but unresponsive helper restarts");
        }
        {
            ClipboardHelperClient client({});
            if (!start(client, "--hung"))
                return false;
            client.m_ConfigSentTicks = SDL_GetTicks() - 30001;
            client.processProtocolLine(ClipboardIpc::encodePong(1).toStdString());
            client.processPendingMessages();
            check(client.m_Process == nullptr, "pong cannot hide missing READY timeout");
        }
        {
            ClipboardHelperClient client({});
            if (!start(client, "--hung"))
                return false;
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
            client.m_Process->write(std::string(ClipboardHelperClient::MAX_PENDING_STDIN_BYTES, 'x'));
#else
            client.m_Process->write(
                QByteArray(ClipboardHelperClient::MAX_PENDING_STDIN_BYTES, 'x'));
#endif
            check(!client.writeLine(std::string(65536, 'y')) && client.m_Process == nullptr,
                  "process write backlog counts toward limit");
        }
        {
            ClipboardHelperClient client({});
            if (!start(client, "--hung"))
                return false;
            const auto frame = ClipboardIpc::encodeLocalFrame(1, QByteArray(60000, 'x')).toStdString();
            for (int i = 0; i < 20; ++i)
                client.m_StdoutBuffer += frame + '\n';
            client.readHelperOutput();
            check(client.m_Process != nullptr && client.m_StdoutBuffer.empty(),
                  "legal aggregated stdout does not restart helper");
        }
        return ok;
    }
};
int main(int argc, char** argv)
{
    const char* mode = argc == 2 ? argv[1] : std::getenv("MOONLIGHT_CLIPBOARD_CLIENT_TEST_CHILD");
    if (mode && !std::strcmp(mode, "--reflect-frames")) {
        std::string line;
        while (std::getline(std::cin, line)) {
            ClipboardIpc::Message message;
            QString error;
            if (!ClipboardIpc::decodeLine(QByteArray::fromStdString(line), message, error)) return 2;
            if (message.type == ClipboardIpc::MessageType::Stop) break;
            QByteArray result;
            if (message.type == ClipboardIpc::MessageType::Configure) {
                const auto& host = message.config;
                if (host.address != QStringLiteral("localhost") || host.httpsPort != 47984 ||
                    host.serverCertPem != "server" || host.clientCertPem != "client" ||
                    host.clientKeyPem != "key") {
                    return 3;
                }
                result = ClipboardIpc::encodeReady(message.sequence);
            } else if (message.type == ClipboardIpc::MessageType::HostFrame) {
                result = ClipboardIpc::encodeLocalFrame(message.sequence, message.frame);
            }
            if (!result.isEmpty()) {
                std::fwrite(result.constData(), 1, static_cast<size_t>(result.size()), stdout);
                std::fputc('\n', stdout);
                std::fflush(stdout);
            }
        }
        return 0;
    }
    if (mode && !std::strcmp(mode, "--invalid-output")) {
        std::puts("invalid JSON");
        return 0;
    }
    if (mode && !std::strcmp(mode, "--hung")) {
        QThread::sleep(15);
        return 0;
    }
    std::fputs("Starting clipboard helper lifecycle regression\n", stderr);
    std::fflush(stderr);
    TestDeadline deadline;
    QCoreApplication app(argc, argv);
    QTextStream err(stderr), out(stdout);
    if (!ClipboardHelperClientTest::run(err))
        return 1;
    const auto args = app.arguments();
    if (args.size() == 3 && !checkPackagedHelper(args[1], QDir(args[2]), err))
        return 1;
    out << "clipboard_helper_lifecycle=passed\n";
    return 0;
}
