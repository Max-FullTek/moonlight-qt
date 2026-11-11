#define SDL_MAIN_HANDLED
#include "streaming/clipboardhelperclient.h"
#include "streaming/clipboardipc.h"
#include "backend/identitymanager.h"
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

// The test exercises process supervision, never the user's paired identity or
// a real streaming connection. Keep those external dependencies inert.
IdentityManager* IdentityManager::get()
{
    return nullptr;
}
QByteArray IdentityManager::getCertificate()
{
    return QByteArray();
}
QByteArray IdentityManager::getPrivateKey()
{
    return QByteArray();
}
static std::vector<std::string> sentFrames;
extern "C" int LiSendClipboardData(const void* data, int length)
{
    sentFrames.emplace_back(static_cast<const char*>(data), static_cast<size_t>(length));
    return 0;
}

class ClipboardHelperClientTest
{
public:
    static bool start(ClipboardHelperClient& client, const QString& mode)
    {
        std::fprintf(stderr, "Starting lifecycle case: %s\n", qPrintable(mode));
        std::fflush(stderr);
        client.m_Enabled = true;
        client.m_Process = new QProcess(&client);
        client.m_Process->start(QCoreApplication::applicationFilePath(), { mode });
        std::fputs("Helper process launched; initializing timer\n", stderr);
        std::fflush(stderr);
        client.m_LastPingTicks = client.m_LastResponseTicks = SDL_GetTicks();
        return client.m_Process->waitForStarted(3000);
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
            ClipboardHelperClient client(nullptr);
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
            client.m_HelperReady = true;
            sentFrames.clear();
            client.flushQueuedHostFrames();
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (sentFrames.size() < expected.size() && std::chrono::steady_clock::now() < until) {
                if (!client.m_Process) break;
                client.m_Process->waitForReadyRead(100);
                client.readHelperOutput();
            }
            check(sentFrames == expected, "cross-thread frames preserve binary content and FIFO order through helper IPC");
            check(client.m_InboundFrames.empty() && client.m_DroppedInboundFrames == 0,
                  "flushing consumes queued frames and drop count");
        }
#if defined(Q_OS_WIN) && defined(NDEBUG)
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
            ClipboardHelperClient client(nullptr);
            const QString expected = QDir(QCoreApplication::applicationDirPath())
                .filePath("moonlight-clipboard-helper.exe");
            const QString resolved = client.findHelperExecutable();
            check(resolved != fakePath, "release helper ignores environment and working directory");
            check(resolved == (QFileInfo(expected).isFile() ? expected : QString()),
                  "release helper uses bundled executable or disables clipboard");
            QDir::setCurrent(previousDirectory);
            if (hadOverride) qputenv("MOONLIGHT_CLIPBOARD_HELPER", previousOverride);
            else qunsetenv("MOONLIGHT_CLIPBOARD_HELPER");
        }
#endif
        {
            ClipboardHelperClient client(nullptr);
            if (!start(client, "--invalid-output"))
                return false;
            check(client.m_Process->waitForFinished(3000), "fake helper exits");
            client.processPendingMessages();
            check(client.m_Process == nullptr && client.m_RestartAttempts == 1,
                  "invalid trailing output schedules one restart without null dereference");
        }
        {
            ClipboardHelperClient client(nullptr);
            if (!start(client, "--hung"))
                return false;
            client.m_LastResponseTicks = SDL_GetTicks() - 30001;
            client.processPendingMessages();
            check(client.m_Process == nullptr && client.m_RestartAttempts == 1,
                  "living but unresponsive helper restarts");
        }
        {
            ClipboardHelperClient client(nullptr);
            if (!start(client, "--hung"))
                return false;
            client.m_ConfigSentTicks = SDL_GetTicks() - 30001;
            client.processProtocolLine(ClipboardIpc::encodePong(1));
            client.processPendingMessages();
            check(client.m_Process == nullptr, "pong cannot hide missing READY timeout");
        }
        {
            ClipboardHelperClient client(nullptr);
            if (!start(client, "--hung"))
                return false;
            client.m_Process->write(
                QByteArray(ClipboardHelperClient::MAX_PENDING_STDIN_BYTES, 'x'));
            check(!client.writeLine(QByteArray(65536, 'y')) && client.m_Process == nullptr,
                  "QProcess internal write backlog counts toward limit");
        }
        {
            ClipboardHelperClient client(nullptr);
            if (!start(client, "--hung"))
                return false;
            const auto frame = ClipboardIpc::encodeLocalFrame(1, QByteArray(60000, 'x'));
            for (int i = 0; i < 20; ++i)
                client.m_StdoutBuffer += frame + '\n';
            client.readHelperOutput();
            check(client.m_Process != nullptr && client.m_StdoutBuffer.isEmpty(),
                  "legal aggregated stdout does not restart helper");
        }
        return ok;
    }
};
int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--reflect-frames")) {
        std::string line;
        while (std::getline(std::cin, line)) {
            ClipboardIpc::Message message;
            QString error;
            if (!ClipboardIpc::decodeLine(QByteArray::fromStdString(line), message, error)) return 2;
            if (message.type == ClipboardIpc::MessageType::Stop) break;
            if (message.type == ClipboardIpc::MessageType::HostFrame) {
                const auto result = ClipboardIpc::encodeLocalFrame(message.sequence, message.frame);
                std::fwrite(result.constData(), 1, static_cast<size_t>(result.size()), stdout);
                std::fputc('\n', stdout);
                std::fflush(stdout);
            }
        }
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--invalid-output")) {
        std::puts("invalid JSON");
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--hung")) {
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
    out << "clipboard_helper_lifecycle=passed\n";
    return 0;
}
