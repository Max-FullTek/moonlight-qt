#pragma once

#include "nativeclipboardipc.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <deque>
#include <mutex>
#include <string>

#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
class WindowsClipboardProcess;
#else
class QProcess;
#endif

class ClipboardHelperClient
{
public:
    using HostConfigProvider = std::function<NativeClipboardIpc::Host()>;

    explicit ClipboardHelperClient(HostConfigProvider hostConfig,
                                   std::function<void()> unsupportedFormat = {});
    ~ClipboardHelperClient();

    void start();
    void stop();
    void updateHostContext();

    // Thread-safe. Called from the Limelight control receive thread.
    void handleIncomingFrame(const char* data, int length);

    // Called from the SDL streaming loop on the session/main thread.
    void processPendingMessages();

    bool isRunning() const;

private:
    friend class ClipboardHelperClientTest;
    static constexpr int MAX_QUEUED_HOST_FRAMES = 32;
    static constexpr int MAX_PENDING_STDIN_BYTES = 4 * 1024 * 1024;
    static constexpr int MAX_RESTART_ATTEMPTS = 3;
    static constexpr uint32_t RESTART_DELAY_MS = 1000;

    std::filesystem::path findHelperExecutable() const;
    bool sendCurrentConfig();
    bool startProcess();
    void scheduleRestart(const char* reason);
    void maybeRestart();
    void flushQueuedHostFrames();
    void readHelperOutput();
    void readHelperErrors();
    void logHelperStderrLine(std::string_view line);
    void processProtocolLine(std::string_view line);
    void handleProtocolError(const std::string& error);
    void restartHelper(const char* reason);
    bool flushProcessInput();
    bool writeLine(std::string line);
    uint32_t nextSequence();

    HostConfigProvider m_HostConfig;
    std::function<void()> m_UnsupportedFormat;
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    std::unique_ptr<WindowsClipboardProcess> m_Process;
#else
    std::unique_ptr<QProcess> m_Process;
#endif
    std::filesystem::path m_HelperPath;
    std::mutex m_InboundMutex;
    std::deque<std::string> m_InboundFrames;
    std::string m_StdoutBuffer;
    std::string m_StderrBuffer;
    std::string m_StdinBuffer;
    bool m_Enabled;
    bool m_Disabled;
    bool m_StopRequested;
    bool m_HelperReady;
    uint32_t m_ConfigSequence;
    uint32_t m_NextSequence;
    uint32_t m_NextRestartTicks;
    uint32_t m_LastResponseTicks = 0;
    uint32_t m_LastPingTicks = 0;
    uint32_t m_ConfigSentTicks = 0;
    int m_RestartAttempts;
    int m_DroppedInboundFrames;
};
