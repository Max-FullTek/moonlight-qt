#include "clipboardhelperclient.h"

#include "backend/identitymanager.h"
#include "backend/nvcomputer.h"
#include "clipboardipc.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
#include "windowsclipboardprocess.h"
#else
#include <QProcess>
#endif

#include <SDL.h>

#include <Limelight.h>

ClipboardHelperClient::ClipboardHelperClient(NvComputer* computer, QObject* parent)
    : QObject(parent),
      m_Computer(computer),
      m_Process(nullptr),
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
    if (m_HelperPath.isEmpty()) {
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

#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
    m_Process = new WindowsClipboardProcess;
    if (!m_Process->start(m_HelperPath.toStdWString())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard helper failed to start");
#else
    m_Process = new QProcess(this);
    m_Process->setProgram(m_HelperPath);
    m_Process->setProcessChannelMode(QProcess::SeparateChannels);
    m_Process->start();
    if (!m_Process->waitForStarted(2000)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper failed to start: %s",
                    m_Process->errorString().toUtf8().constData());
#endif
        delete m_Process;
        m_Process = nullptr;
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
                m_HelperPath.toUtf8().constData());

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
        writeLine(ClipboardIpc::encodeStop(nextSequence()));
        flushProcessInput();
        if (m_Process != nullptr) {
#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
            m_Process->closeInput();
#else
            m_Process->closeWriteChannel();
#endif
        }
#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
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

    if (m_Process != nullptr) {
        delete m_Process;
        m_Process = nullptr;
    }
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
#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard helper exited: exitCode=%u", m_Process->exitCode());
#else
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper exited: exitCode=%d error=%s",
                    m_Process->exitCode(),
                    m_Process->errorString().toUtf8().constData());
#endif
        delete m_Process;
        m_Process = nullptr;
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

#if !defined(Q_OS_WIN) || !defined(LITE_NATIVE_HTTP)
    m_Process->waitForReadyRead(0);
#endif
    readHelperOutput();
    readHelperErrors();
    if (m_Process == nullptr) {
        return;
    }
    const quint32 now = SDL_GetTicks();
    if (SDL_TICKS_PASSED(now, m_LastResponseTicks + 30000) ||
        (!m_HelperReady && SDL_TICKS_PASSED(now, m_ConfigSentTicks + 30000))) {
        restartHelper("helper response timeout");
        return;
    }
    if (SDL_TICKS_PASSED(now, m_LastPingTicks + 5000)) {
        m_LastPingTicks = now;
        if (!writeLine(ClipboardIpc::encodePing(nextSequence()))) {
            return;
        }
    }
    if (m_HelperReady) {
        flushQueuedHostFrames();
    }
}

bool ClipboardHelperClient::isRunning() const
{
#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
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
    if (!m_Enabled || m_Disabled || m_StopRequested || m_HelperPath.isEmpty()) {
        return;
    }

    if (m_NextRestartTicks == 0 || SDL_TICKS_PASSED(SDL_GetTicks(), m_NextRestartTicks)) {
        m_NextRestartTicks = 0;
        startProcess();
    }
}

QString ClipboardHelperClient::findHelperExecutable() const
{
#ifdef Q_OS_WIN
    const QString helperName = QStringLiteral("moonlight-clipboard-helper.exe");
#else
    const QString helperName = QStringLiteral("moonlight-clipboard-helper");
#endif

#if defined(Q_OS_WIN) && defined(NDEBUG)
    // Release builds only trust the helper shipped beside the application.
    // Never send paired credentials to an environment/CWD-selected executable.
    const QFileInfo bundled(QDir(QCoreApplication::applicationDirPath()).filePath(helperName));
    return bundled.isFile() ? bundled.absoluteFilePath() : QString();
#else
    QString envPath = QString::fromLocal8Bit(qgetenv("MOONLIGHT_CLIPBOARD_HELPER"));
    if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
        return envPath;
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
            return cleanPath;
        }
    }

    return QString();
#endif
}

bool ClipboardHelperClient::sendCurrentConfig()
{
    ClipboardIpc::HostConfig config;
    if (m_Computer != nullptr) {
        config.address = m_Computer->activeAddress.address();
        config.httpsPort = m_Computer->activeHttpsPort;
        config.serverCertPem = m_Computer->serverCert.toPem();
    }

    IdentityManager* identity = IdentityManager::get();
    if (identity != nullptr) {
        config.clientCertPem = identity->getCertificate();
        config.clientKeyPem = identity->getPrivateKey();
    }

    if (config.address.isEmpty() || config.httpsPort == 0 ||
            config.serverCertPem.isEmpty() ||
            config.clientCertPem.isEmpty() ||
            config.clientKeyPem.isEmpty()) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "Clipboard helper config is not available yet");
        return false;
    }

    m_HelperReady = false;
    m_ConfigSequence = nextSequence();
    m_ConfigSentTicks = SDL_GetTicks();
    return writeLine(ClipboardIpc::encodeConfigure(m_ConfigSequence, config));
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

    for (const auto& frame : frames) {
        const auto bytes = QByteArray::fromRawData(frame.data(), static_cast<int>(frame.size()));
        if (!writeLine(ClipboardIpc::encodeHostFrame(nextSequence(), bytes))) {
            return;
        }
    }
}

void ClipboardHelperClient::readHelperOutput()
{
    if (m_Process == nullptr) {
        return;
    }

#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
    m_StdoutBuffer += QByteArray::fromStdString(m_Process->readOutput());
#else
    m_StdoutBuffer += m_Process->readAllStandardOutput();
#endif
    QByteArray line;
    QString error;
    while (ClipboardIpc::takeLine(m_StdoutBuffer, line, error)) {
        processProtocolLine(line);
        if (m_Process == nullptr) {
            return;
        }
    }
    if (!error.isEmpty()) {
        handleProtocolError(error);
    }
}

void ClipboardHelperClient::readHelperErrors()
{
    if (m_Process == nullptr) {
        return;
    }

#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
    m_StderrBuffer += QByteArray::fromStdString(m_Process->readErrors());
#else
    m_StderrBuffer += m_Process->readAllStandardError();
#endif
    if (m_StderrBuffer.size() > ClipboardIpc::MAX_LINE_BYTES) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper stderr line exceeded protocol limit; discarding buffered stderr");
        m_StderrBuffer.clear();
        return;
    }

    int newlineIndex = -1;
    while ((newlineIndex = m_StderrBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_StderrBuffer.left(newlineIndex);
        m_StderrBuffer.remove(0, newlineIndex + 1);
        while (line.endsWith('\r')) {
            line.chop(1);
        }
        if (!line.isEmpty()) {
            logHelperStderrLine(line);
        }
    }
}

void ClipboardHelperClient::logHelperStderrLine(const QByteArray& line)
{
    if (line.startsWith("ClipboardSync WARN:")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper: %s",
                    line.constData());
        return;
    }

    if (line.startsWith("ClipboardSync INFO:")) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper: %s",
                    line.constData());
        return;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "Clipboard helper: %s",
                 line.constData());
}

void ClipboardHelperClient::processProtocolLine(const QByteArray& line)
{
    ClipboardIpc::Message message;
    QString error;
    if (!ClipboardIpc::decodeLine(line, message, error)) {
        handleProtocolError(error);
        return;
    }
    m_LastResponseTicks = SDL_GetTicks();
    if (message.type == ClipboardIpc::MessageType::Pong) {
        return;
    }

    if (message.type == ClipboardIpc::MessageType::Ready) {
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

    if (message.type == ClipboardIpc::MessageType::LocalFrame) {
        if (!m_HelperReady) {
            return;
        }
        int rc = LiSendClipboardData(message.frame.constData(), message.frame.size());
        if (rc != 0) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                         "LiSendClipboardData(helper, %d bytes) -> %d",
                         static_cast<int>(message.frame.size()),
                         rc);
        }
        return;
    }

    if (message.type == ClipboardIpc::MessageType::Error) {
        if (m_HelperReady && message.sequence == 0 &&
                message.code == QStringLiteral("unsupported-format")) {
            emit unsupportedFormat();
            return;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper error [%s]: %s",
                    message.code.toUtf8().constData(),
                    message.text.toUtf8().constData());
        return;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "Clipboard helper sent unexpected message type");
}

void ClipboardHelperClient::handleProtocolError(const QString& error)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Clipboard helper protocol error: %s",
                error.toUtf8().constData());
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
#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
            m_Process->terminate();
            m_Process->wait(1000);
#else
            m_Process->kill();
            m_Process->waitForFinished(1000);
#endif
        }
        delete m_Process;
        m_Process = nullptr;
    }

    scheduleRestart(reason);
}

bool ClipboardHelperClient::flushProcessInput()
{
    if (!isRunning()) {
        return false;
    }

#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
    bool success = m_Process->flush();
    if (success && m_Process->pendingBytes() == 0 && !m_StdinBuffer.isEmpty()) {
        success = m_Process->write({m_StdinBuffer.constData(), static_cast<size_t>(m_StdinBuffer.size())});
        if (success) m_StdinBuffer.clear();
    }
    if (!success) restartHelper("pipe write failed");
    return success;
#else
    while (!m_StdinBuffer.isEmpty()) {
        qint64 written = m_Process->write(m_StdinBuffer.constData(), m_StdinBuffer.size());
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

        m_StdinBuffer.remove(0, static_cast<int>(written));
    }

    m_Process->waitForBytesWritten(0);
    return true;
#endif
}

bool ClipboardHelperClient::writeLine(const QByteArray& line)
{
    if (!isRunning()) {
        return false;
    }

    QByteArray out = line;
    out.append('\n');
#if defined(Q_OS_WIN) && defined(LITE_NATIVE_HTTP)
    const auto pendingBytes = m_Process->pendingBytes();
#else
    const auto pendingBytes = m_Process->bytesToWrite();
#endif
    if (pendingBytes + m_StdinBuffer.size() + out.size() > MAX_PENDING_STDIN_BYTES) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Clipboard helper stdin backlog exceeded protocol limit");
        restartHelper("pipe backlog exceeded");
        return false;
    }

    m_StdinBuffer.append(out);
    return flushProcessInput();
}

quint32 ClipboardHelperClient::nextSequence()
{
    if (m_NextSequence == 0) {
        m_NextSequence = 1;
    }
    return m_NextSequence++;
}
