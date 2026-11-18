#include "clipboardhelperclient.h"

#include "clipboardpipelines.h"

#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
#include "windowsclipboardprocess.h"
#include <windows.h>
#else
#include <QProcess>
#endif

#if !defined(_WIN32) || !defined(LITE_NATIVE_HTTP)
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#endif

#if !defined(LITE_NATIVE_HTTP) || !defined(LITE_NATIVE_WORKER_JSON)
#include "clipboardipc.h"
#endif

#include <cstdlib>
#include <utility>

#include <SDL.h>

#include <Limelight.h>

namespace {
using NativeClipboardIpc::Message;
using NativeClipboardIpc::Type;

std::string encodeMessage(const Message& message)
{
#if defined(LITE_NATIVE_HTTP) && defined(LITE_NATIVE_WORKER_JSON)
    return NativeClipboardIpc::encode(message);
#else
    switch (message.type) {
    case Type::Configure:
        return ClipboardIpc::encodeConfigure(message.sequence, {
            QString::fromStdString(message.host.address), message.host.httpsPort,
            QByteArray::fromStdString(message.host.serverCertPem),
            QByteArray::fromStdString(message.host.clientCertPem),
            QByteArray::fromStdString(message.host.clientKeyPem)}).toStdString();
    case Type::HostFrame:
        return ClipboardIpc::encodeHostFrame(message.sequence, QByteArray::fromRawData(
            message.frame.data(), static_cast<int>(message.frame.size()))).toStdString();
    case Type::Ping:
        return ClipboardIpc::encodePing(message.sequence).toStdString();
    case Type::Stop:
        return ClipboardIpc::encodeStop(message.sequence).toStdString();
    default:
        SDL_assert(false);
        return {};
    }
#endif
}

bool decodeMessage(std::string_view line, Message& message, std::string& error)
{
#if defined(LITE_NATIVE_HTTP) && defined(LITE_NATIVE_WORKER_JSON)
    return NativeClipboardIpc::decode(line, message, error);
#else
    ClipboardIpc::Message decoded;
    QString reason;
    if (!ClipboardIpc::decodeLine(QByteArray::fromRawData(line.data(),
                                static_cast<int>(line.size())), decoded, reason)) {
        error = reason.toStdString();
        return false;
    }
    message.type = decoded.type;
    message.sequence = decoded.sequence;
    message.frame = decoded.frame.toStdString();
    message.code = decoded.code.toStdString();
    message.text = decoded.text.toStdString();
    return true;
#endif
}
}

ClipboardHelperClient::ClipboardHelperClient(HostConfigProvider hostConfig,
                                           std::function<void()> unsupportedFormat)
    : m_HostConfig(std::move(hostConfig)),
      m_UnsupportedFormat(std::move(unsupportedFormat)),
      m_Enabled(false),
      m_Disabled(false),
      m_StopRequested(false),
      m_HelperReady(false),
      m_ConfigSequence(0),
      m_NextSequence(1),
      m_NextRestartTicks(0),
      m_RestartAttempts(0),
      m_DroppedInboundFrames(0)
{
}

ClipboardHelperClient::~ClipboardHelperClient()
{
    stop();
}

void ClipboardHelperClient::start()
{
    if (m_Enabled) {
        return;
    }

    m_Enabled = true;
    m_Disabled = false;
    m_StopRequested = false;
    m_RestartAttempts = 0;
    m_NextRestartTicks = 0;
    m_HelperPath = findHelperExecutable();
    if (m_HelperPath.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper executable not found; clipboard sync disabled");
        m_Disabled = true;
        return;
    }

    startProcess();
}

bool ClipboardHelperClient::startProcess()
{
    if (!m_Enabled || m_Disabled || m_Process != nullptr) {
        return false;
    }

#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    m_Process = std::make_unique<WindowsClipboardProcess>();
    if (!m_Process->start(m_HelperPath.native())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard helper failed to start");
#else
    m_Process = std::make_unique<QProcess>();
    m_Process->setProgram(QString::fromStdU16String(m_HelperPath.u16string()));
    m_Process->setProcessChannelMode(QProcess::SeparateChannels);
    m_Process->start();
    if (!m_Process->waitForStarted(2000)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper failed to start: %s",
                    m_Process->errorString().toUtf8().constData());
#endif
        m_Process.reset();
        scheduleRestart("start failed");
        return false;
    }

    m_HelperReady = false;
    m_StdoutBuffer.clear();
    m_StderrBuffer.clear();
    m_StdinBuffer.clear();
    m_ConfigSentTicks = m_LastResponseTicks = m_LastPingTicks = SDL_GetTicks();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Clipboard helper started: %s",
                m_HelperPath.u8string().c_str());

    sendCurrentConfig();
    return true;
}

void ClipboardHelperClient::stop()
{
    m_Enabled = false;
    m_StopRequested = true;
    m_HelperReady = false;
    m_NextRestartTicks = 0;

    if (m_Process == nullptr) {
        return;
    }

    if (isRunning()) {
        writeLine(encodeMessage({Type::Stop, nextSequence()}));
        flushProcessInput();
        if (m_Process != nullptr) {
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
            m_Process->closeInput();
#else
            m_Process->closeWriteChannel();
#endif
        }
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
        if (m_Process != nullptr && !m_Process->wait(1000)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard helper did not exit cleanly; killing it");
            m_Process->terminate();
            m_Process->wait(1000);
        }
#else
        if (m_Process != nullptr && !m_Process->waitForFinished(1000)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Clipboard helper did not exit cleanly; killing it");
            m_Process->kill();
            m_Process->waitForFinished(1000);
        }
#endif
    }

    m_Process.reset();
}

void ClipboardHelperClient::updateHostContext()
{
    if (!m_Enabled || m_Disabled || !isRunning()) {
        return;
    }

    sendCurrentConfig();
}

void ClipboardHelperClient::handleIncomingFrame(const char* data, int length)
{
    if (data == nullptr || length <= 0) {
        return;
    }

    std::lock_guard locker(m_InboundMutex);
    if (m_InboundFrames.size() >= MAX_QUEUED_HOST_FRAMES) {
        m_InboundFrames.pop_front();
        m_DroppedInboundFrames++;
    }
    m_InboundFrames.emplace_back(data, static_cast<size_t>(length));
}

void ClipboardHelperClient::processPendingMessages()
{
    if (m_Process == nullptr) {
        maybeRestart();
        return;
    }

    if (!isRunning()) {
        readHelperOutput();
        readHelperErrors();
        // Invalid trailing output may have already destroyed the process.
        if (m_Process == nullptr) {
            return;
        }
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard helper exited: exitCode=%u", m_Process->exitCode());
#else
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper exited: exitCode=%d error=%s",
                    m_Process->exitCode(),
                    m_Process->errorString().toUtf8().constData());
#endif
        m_Process.reset();
        m_HelperReady = false;
        m_StdinBuffer.clear();
        if (!m_StopRequested) {
            scheduleRestart("unexpected exit");
        }
        return;
    }

    if (!flushProcessInput()) {
        return;
    }

#if !defined(_WIN32) || !defined(LITE_NATIVE_HTTP)
    m_Process->waitForReadyRead(0);
#endif
    readHelperOutput();
    readHelperErrors();
    if (m_Process == nullptr) {
        return;
    }
    const uint32_t now = SDL_GetTicks();
    if (SDL_TICKS_PASSED(now, m_LastResponseTicks + 30000) ||
        (!m_HelperReady && SDL_TICKS_PASSED(now, m_ConfigSentTicks + 30000))) {
        restartHelper("helper response timeout");
        return;
    }
    if (SDL_TICKS_PASSED(now, m_LastPingTicks + 5000)) {
        m_LastPingTicks = now;
        if (!writeLine(encodeMessage({Type::Ping, nextSequence()}))) {
            return;
        }
    }
    if (m_HelperReady) {
        flushQueuedHostFrames();
    }
}

bool ClipboardHelperClient::isRunning() const
{
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    return m_Process != nullptr && m_Process->running();
#else
    return m_Process != nullptr && m_Process->state() != QProcess::NotRunning;
#endif
}

void ClipboardHelperClient::scheduleRestart(const char* reason)
{
    m_HelperReady = false;
    if (!m_Enabled || m_Disabled || m_StopRequested) {
        return;
    }

    if (m_RestartAttempts >= MAX_RESTART_ATTEMPTS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper disabled after %d restart attempts (%s)",
                    m_RestartAttempts,
                    reason);
        m_Disabled = true;
        return;
    }

    m_RestartAttempts++;
    m_NextRestartTicks = SDL_GetTicks() + RESTART_DELAY_MS;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Clipboard helper will restart after %s (attempt %d/%d)",
                reason,
                m_RestartAttempts,
                MAX_RESTART_ATTEMPTS);
}

void ClipboardHelperClient::maybeRestart()
{
    if (!m_Enabled || m_Disabled || m_StopRequested || m_HelperPath.empty()) {
        return;
    }

    if (m_NextRestartTicks == 0 || SDL_TICKS_PASSED(SDL_GetTicks(), m_NextRestartTicks)) {
        m_NextRestartTicks = 0;
        startProcess();
    }
}

std::filesystem::path ClipboardHelperClient::findHelperExecutable() const
{
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    // The helper receives paired credentials. Release builds only use the bundled executable.
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(),
                                           static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return {};
    }
    executable.resize(length);

    const auto appDir = std::filesystem::path(executable).parent_path();
    constexpr auto helperName = L"moonlight-clipboard-helper.exe";
    std::error_code error;
#ifdef NDEBUG
    const auto bundled = appDir / helperName;
    return std::filesystem::is_regular_file(bundled, error) ? bundled : std::filesystem::path{};
#else
    if (const auto* overridePath = _wgetenv(L"MOONLIGHT_CLIPBOARD_HELPER");
        overridePath && *overridePath && std::filesystem::exists(overridePath, error)) {
        return overridePath;
    }

    const std::filesystem::path roots[] = {
        appDir, appDir / L"../clipboard-helper/release",
        appDir / L"../clipboard-helper/debug", appDir / L"../clipboard-helper",
        appDir / L"../../clipboard-helper/release", appDir / L"../../clipboard-helper/debug",
        appDir / L"../../clipboard-helper", std::filesystem::current_path(error)
    };
    for (const auto& root : roots) {
        if (root.empty()) {
            continue;
        }
        auto candidate = (root / helperName).lexically_normal();
        if (std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
#endif
#else
#ifdef _WIN32
    const QString helperName = QStringLiteral("moonlight-clipboard-helper.exe");
#else
    const QString helperName = QStringLiteral("moonlight-clipboard-helper");
#endif

#if defined(_WIN32) && defined(NDEBUG)
    // Release builds only trust the helper shipped beside the application.
    // Never send paired credentials to an environment/CWD-selected executable.
    const QFileInfo bundled(QDir(QCoreApplication::applicationDirPath()).filePath(helperName));
    return bundled.isFile() ? std::filesystem::path(bundled.absoluteFilePath().toStdU16String()) : std::filesystem::path{};
#else
    QString envPath = QString::fromLocal8Bit(qgetenv("MOONLIGHT_CLIPBOARD_HELPER"));
    if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
        return std::filesystem::path(envPath.toStdU16String());
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(helperName),
        QDir(appDir).filePath(QStringLiteral("../clipboard-helper/release/") + helperName),
        QDir(appDir).filePath(QStringLiteral("../clipboard-helper/debug/") + helperName),
        QDir(appDir).filePath(QStringLiteral("../clipboard-helper/") + helperName),
        QDir(appDir).filePath(QStringLiteral("../../clipboard-helper/release/") + helperName),
        QDir(appDir).filePath(QStringLiteral("../../clipboard-helper/debug/") + helperName),
        QDir(appDir).filePath(QStringLiteral("../../clipboard-helper/") + helperName),
        QDir(QDir::currentPath()).filePath(helperName)
    };

    for (const QString& candidate : candidates) {
        const QString cleanPath = QDir::cleanPath(candidate);
        if (QFileInfo::exists(cleanPath)) {
            return std::filesystem::path(cleanPath.toStdU16String());
        }
    }

    return {};
#endif
#endif
}

bool ClipboardHelperClient::sendCurrentConfig()
{
    if (!m_HostConfig) {
        return false;
    }
    auto config = m_HostConfig();
    if (config.address.empty() || config.httpsPort == 0 ||
            config.serverCertPem.empty() ||
            config.clientCertPem.empty() ||
            config.clientKeyPem.empty()) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "Clipboard helper config is not available yet");
        return false;
    }

    m_HelperReady = false;
    m_ConfigSequence = nextSequence();
    m_ConfigSentTicks = SDL_GetTicks();
    return writeLine(encodeMessage({Type::Configure, m_ConfigSequence, std::move(config)}));
}

void ClipboardHelperClient::flushQueuedHostFrames()
{
    std::deque<std::string> frames;
    int droppedFrames = 0;
    {
        std::lock_guard locker(m_InboundMutex);
        frames.swap(m_InboundFrames);
        droppedFrames = m_DroppedInboundFrames;
        m_DroppedInboundFrames = 0;
    }

    if (droppedFrames != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Dropped %d queued clipboard frames while helper was busy",
                    droppedFrames);
    }

    for (auto& frame : frames) {
        if (!writeLine(encodeMessage({Type::HostFrame, nextSequence(), {}, std::move(frame)}))) {
            return;
        }
    }
}

void ClipboardHelperClient::readHelperOutput()
{
    if (m_Process == nullptr) {
        return;
    }

#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    m_StdoutBuffer += m_Process->readOutput();
#else
    const auto bytes = m_Process->readAllStandardOutput();
    m_StdoutBuffer.append(bytes.constData(), static_cast<size_t>(bytes.size()));
#endif
    size_t consumed = 0;
    while (true) {
        auto remaining = std::string_view(m_StdoutBuffer).substr(consumed);
        const auto boundary = ClipboardPipe::findLine(remaining);
        if (boundary.status == ClipboardPipe::LineStatus::Oversized) {
            handleProtocolError("helper stdout line exceeded protocol limit");
            return;
        }
        if (boundary.status == ClipboardPipe::LineStatus::Incomplete) {
            break;
        }
        auto line = remaining.substr(0, boundary.length);
        while (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        consumed += boundary.length + 1;
        processProtocolLine(line);
        if (m_Process == nullptr) {
            return;
        }
    }
    m_StdoutBuffer.erase(0, consumed);
}

void ClipboardHelperClient::readHelperErrors()
{
    if (m_Process == nullptr) {
        return;
    }

#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    m_StderrBuffer += m_Process->readErrors();
#else
    const auto bytes = m_Process->readAllStandardError();
    m_StderrBuffer.append(bytes.constData(), static_cast<size_t>(bytes.size()));
#endif
    if (m_StderrBuffer.size() > ClipboardPipe::MaxLineBytes) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper stderr line exceeded protocol limit; discarding buffered stderr");
        m_StderrBuffer.clear();
        return;
    }

    size_t consumed = 0;
    for (size_t end = m_StderrBuffer.find('\n'); end != std::string::npos;
         end = m_StderrBuffer.find('\n', consumed)) {
        auto line = std::string_view(m_StderrBuffer).substr(consumed, end - consumed);
        consumed = end + 1;
        while (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            logHelperStderrLine(line);
        }
    }
    m_StderrBuffer.erase(0, consumed);
}

void ClipboardHelperClient::logHelperStderrLine(std::string_view line)
{
    if (line.rfind("ClipboardSync WARN:", 0) == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper: %.*s",
                    static_cast<int>(line.size()), line.data());
        return;
    }

    if (line.rfind("ClipboardSync INFO:", 0) == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper: %.*s",
                    static_cast<int>(line.size()), line.data());
        return;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "Clipboard helper: %.*s",
                 static_cast<int>(line.size()), line.data());
}

void ClipboardHelperClient::processProtocolLine(std::string_view line)
{
    Message message;
    std::string error;
    if (!decodeMessage(line, message, error)) {
        handleProtocolError(error);
        return;
    }
    m_LastResponseTicks = SDL_GetTicks();
    if (message.type == Type::Pong) {
        return;
    }

    if (message.type == Type::Ready) {
        if (message.sequence != m_ConfigSequence) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                         "Clipboard helper READY for stale config sequence %u",
                         message.sequence);
            return;
        }
        m_HelperReady = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard helper is ready");
        return;
    }

    if (message.type == Type::LocalFrame) {
        if (!m_HelperReady) {
            return;
        }
        int rc = LiSendClipboardData(message.frame.data(), static_cast<int>(message.frame.size()));
        if (rc != 0) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                         "LiSendClipboardData(helper, %d bytes) -> %d",
                         static_cast<int>(message.frame.size()),
                         rc);
        }
        return;
    }

    if (message.type == Type::Error) {
        if (m_HelperReady && message.sequence == 0 &&
                message.code == "unsupported-format") {
            if (m_UnsupportedFormat) {
                m_UnsupportedFormat();
            }
            return;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper error [%s]: %s",
                    message.code.c_str(),
                    message.text.c_str());
        return;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "Clipboard helper sent unexpected message type");
}

void ClipboardHelperClient::handleProtocolError(const std::string& error)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Clipboard helper protocol error: %s",
                error.c_str());
    restartHelper("protocol error");
}

void ClipboardHelperClient::restartHelper(const char* reason)
{
    m_HelperReady = false;
    m_StdoutBuffer.clear();
    m_StderrBuffer.clear();
    m_StdinBuffer.clear();

    if (m_Process != nullptr) {
        if (isRunning()) {
#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
            m_Process->terminate();
            m_Process->wait(1000);
#else
            m_Process->kill();
            m_Process->waitForFinished(1000);
#endif
        }
        m_Process.reset();
    }

    scheduleRestart(reason);
}

bool ClipboardHelperClient::flushProcessInput()
{
    if (!isRunning()) {
        return false;
    }

#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    bool success = m_Process->flush();
    if (success && m_Process->pendingBytes() == 0 && !m_StdinBuffer.empty()) {
        success = m_Process->write(m_StdinBuffer);
        if (success) m_StdinBuffer.clear();
    }
    if (!success) restartHelper("pipe write failed");
    return success;
#else
    while (!m_StdinBuffer.empty()) {
        qint64 written = m_Process->write(m_StdinBuffer.data(), static_cast<qint64>(m_StdinBuffer.size()));
        if (written < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Clipboard helper pipe write failed: %s",
                        m_Process->errorString().toUtf8().constData());
            restartHelper("pipe write failed");
            return false;
        }

        if (written == 0) {
            break;
        }

        m_StdinBuffer.erase(0, static_cast<size_t>(written));
    }

    m_Process->waitForBytesWritten(0);
    return true;
#endif
}

bool ClipboardHelperClient::writeLine(std::string line)
{
    if (!isRunning()) {
        return false;
    }

#if defined(_WIN32) && defined(LITE_NATIVE_HTTP)
    const auto pendingBytes = m_Process->pendingBytes();
#else
    const auto pendingBytes = static_cast<size_t>(m_Process->bytesToWrite());
#endif
    if (pendingBytes + m_StdinBuffer.size() + line.size() + 1 > MAX_PENDING_STDIN_BYTES) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper stdin backlog exceeded protocol limit");
        restartHelper("pipe backlog exceeded");
        return false;
    }

    if (m_StdinBuffer.empty()) {
        m_StdinBuffer = std::move(line);
    } else {
        m_StdinBuffer.append(line);
    }
    m_StdinBuffer.push_back('\n');
    return flushProcessInput();
}

uint32_t ClipboardHelperClient::nextSequence()
{
    if (m_NextSequence == 0) {
        m_NextSequence = 1;
    }
    return m_NextSequence++;
}
