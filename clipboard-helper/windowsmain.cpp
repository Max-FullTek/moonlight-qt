#include "backend/windowshttptransfer.h"
#include "streaming/clipboardblobpolicy.h"
#include "streaming/clipboardframe.h"
#include "streaming/clipboardpipelines.h"
#include "streaming/nativeclipboardipc.h"
#include "streaming/windowsclipboardlistener.h"
#include "streaming/windowsclipboardpng.h"

#include <curl/curl.h>
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {
using NativeClipboardIpc::Message;
using NativeClipboardIpc::Type;
using Clock = std::chrono::steady_clock;
constexpr size_t MaxPixels = 32 * 1024 * 1024;
constexpr size_t MaxBytes = ClipboardBlob::MaxBytes;

std::string blobEndpoint(const NativeClipboardIpc::Host& host)
{
    if (!host.httpsPort || host.address.empty() ||
        host.address.find_first_of("/@?#\\ \r\n\t") != std::string::npos ||
        host.address.find('\0') != std::string::npos) return {};
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    auto address = host.address;
    if (address.front() != '[' && address.find(':') != std::string::npos) address = "[" + address + "]";
    if (!url || curl_url_set(url.get(), CURLUPART_SCHEME, "https", 0) ||
        curl_url_set(url.get(), CURLUPART_HOST, address.c_str(), 0) ||
        curl_url_set(url.get(), CURLUPART_PORT, std::to_string(host.httpsPort).c_str(), 0) ||
        curl_url_set(url.get(), CURLUPART_PATH, "/api/v1/clipboard/blob", 0)) return {};
    char* endpoint = nullptr;
    if (curl_url_get(url.get(), CURLUPART_URL, &endpoint, 0)) return {};
    std::unique_ptr<char, decltype(&curl_free)> result(endpoint, curl_free);
    return result.get();
}

std::string toUtf8(std::wstring_view text)
{
    const int length = static_cast<int>(text.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0 || static_cast<size_t>(bytes) > MaxBytes) return {};
    std::string result(bytes, '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), bytes, nullptr, nullptr) != bytes) return {};
    return result;
}

std::wstring fromUtf8(std::string_view text)
{
    if (text.empty() || text.find('\0') != std::string_view::npos) return {};
    const int length = static_cast<int>(text.size());
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    if (chars <= 0) return {};
    std::wstring result(chars, L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), chars) != chars) return {};
    return result;
}

uint64_t hash(std::string_view bytes)
{
    uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) value = (value ^ byte) * 1099511628211ULL;
    return value;
}

uint64_t hashImage(const WindowsClipboardPng::Image& image)
{
    return hash({reinterpret_cast<const char*>(image.rgba.data()), image.rgba.size()}) ^
        (static_cast<uint64_t>(image.width) << 32) ^ image.height;
}

class ClipboardHelper {
public:
    bool handle(ClipboardPipe::LineStatus status, std::string_view line)
    {
        if (status == ClipboardPipe::LineStatus::Incomplete) return false;
        Message message;
        std::string error;
        if (status == ClipboardPipe::LineStatus::Oversized) error = "message line too large";
        else NativeClipboardIpc::decode(line, message, error);
        if (!error.empty()) sendError(0, "bad-message", std::move(error));
        else if (message.type == Type::Stop) return false;
        else if (message.type == Type::Ping) send(Type::Pong, message.sequence);
        else if (message.type == Type::Configure) configure(message.sequence, message.host);
        else if (message.type == Type::HostFrame) {
            if (m_listener) incoming(message.frame);
            else sendError(message.sequence, "not-ready", "clipboard sync is not configured");
        } else {
            sendError(message.sequence, "unexpected-message", "message type is not valid for helper input");
        }
        return !m_outputFailed;
    }

    bool outputFailed() const { return m_outputFailed; }

private:
    struct Retired {
        uint32_t token;
        Clock::time_point time;
    };

    void configure(uint32_t sequence, const NativeClipboardIpc::Host& host)
    {
        const auto endpoint = blobEndpoint(host);
        auto credentials = NativeHttp::parseClipboardCredentials(host.serverCertPem, host.clientCertPem,
                                                                 host.clientKeyPem);
        if (endpoint.empty() || !credentials) {
            sendError(sequence, "bad-config", "missing or invalid host address, port or credentials");
            return;
        }
        if (!m_listener) {
            auto listener = std::make_unique<WindowsClipboardListener>([this] { localChanged(); });
            if (!listener->active()) {
                sendError(sequence, "clipboard-unavailable", "cannot listen for clipboard changes");
                return;
            }
            m_listener = std::move(listener);
        }
        beginChange(0);
        m_retired.clear();
        m_endpoint = endpoint;
        m_credentials = std::move(*credentials);
        m_clipboardSequence = GetClipboardSequenceNumber();
        m_snapshotValid = false;
        send(Type::Ready, sequence);
    }

    void write(const Message& message)
    {
        auto line = NativeClipboardIpc::encode(message);
        line.push_back('\n');
        m_outputFailed = fwrite(line.data(), 1, line.size(), stdout) != line.size() || fflush(stdout) != 0;
    }

    void send(Type type, uint32_t sequence, std::string frame = {})
    {
        Message message;
        message.type = type;
        message.sequence = sequence;
        message.frame = std::move(frame);
        write(message);
    }

    void sendError(uint32_t sequence, std::string code, std::string description)
    {
        Message message;
        message.type = Type::Error;
        message.sequence = sequence;
        message.code = std::move(code);
        message.text = std::move(description);
        write(message);
    }

    void unsupported() { sendError(0, "unsupported-format", "Unsupported clipboard format"); }

    void beginChange(uint32_t token)
    {
        if (m_currentToken) {
            m_retired.push_back({m_currentToken, Clock::now()});
            if (m_retired.size() > 32) m_retired.pop_front();
        }
        m_currentToken = token;
        m_outgoingToken = 0;
        m_outgoingReference.clear();
        m_transfers.clear();
        m_burstText.clear();
        m_burstPng.clear();
        m_burstDeadline = Clock::now() + std::chrono::seconds(5);
    }

    void sendFrame(uint8_t kind, uint32_t token, std::string_view payload)
    {
        const auto header = ClipboardFrame::encodeHeader(kind, token, payload.size());
        if (!header) return;
        std::string frame(header->data(), header->size());
        frame.append(payload);
        send(Type::LocalFrame, m_nextSequence++, std::move(frame));
    }

    bool request(std::string suffix, uint8_t kind, std::string payload, size_t limit,
                 std::function<void(std::string)> complete)
    {
        if (m_transfers.size() >= ClipboardBlob::MaxPendingTransfers) return false;
        const bool upload = !payload.empty();
        NativeHttp::Request request;
        request.url = m_endpoint + suffix;
        request.pinnedCertificateDer = m_credentials.serverDer;
        request.clientCertificatePem = m_credentials.clientCertificatePem;
        request.clientPrivateKeyPem = m_credentials.clientPrivateKeyPem;
        request.timeoutMs = ClipboardBlob::TimeoutMs;
        request.maxResponseBytes = limit;
        request.limitDeclaredLength = true;
        if (upload) {
            request.method = NativeHttp::Method::Post;
            request.contentType = "application/octet-stream";
            request.clipboardMime = kind == ClipboardFrame::Png ? NativeHttp::ClipboardMime::Png :
                                                                  NativeHttp::ClipboardMime::Text;
            request.body = std::move(payload);
        }
        const auto id = ++m_nextTransfer;
        auto transfer = std::make_unique<NativeHttp::WindowsTransfer>(std::move(request),
            [this, id, upload, complete = std::move(complete)](NativeHttp::Response response) {
                m_transfers.erase(id);
                const auto sequence = m_clipboardSequence;
                localChanged();
                if (sequence != m_clipboardSequence && !m_listener->ownsClipboard()) return;
                if (response.error == NativeHttp::Error::None && response.certificateVerified &&
                    response.httpStatus >= 200 && response.httpStatus < 300) {
                    complete(std::move(response.body));
                } else if (upload) m_snapshotValid = false;
            });
        if (!transfer->active()) return false;
        m_transfers.emplace(id, std::move(transfer));
        return true;
    }

    void outbound(uint8_t kind, uint32_t token, std::string payload)
    {
        if (payload.empty() || payload.size() > MaxBytes) return;
        if (payload.size() < ClipboardFrame::InlineThreshold ||
            (kind == ClipboardFrame::Text && token)) {
            sendFrame(kind, token, payload);
            return;
        }
        const auto size = static_cast<int64_t>(payload.size());
        if (!request({}, kind, std::move(payload), ClipboardBlob::MaxUploadReplyBytes,
            [this, kind, token, size](std::string response) {
                const auto id = NativeClipboardIpc::decodeUploadId(response);
                if (!id) {
                    m_snapshotValid = false;
                    return;
                }
                m_outgoingReference = *id;
                sendFrame(ClipboardFrame::Reference, token, NativeClipboardIpc::encodeReference(
                    {*id, kind == ClipboardFrame::Png ? "image/png" : "text/plain", size}));
            })) m_snapshotValid = false;
    }

    void localChanged()
    {
        const auto sequence = GetClipboardSequenceNumber();
        if (sequence == m_clipboardSequence) return;
        m_clipboardSequence = sequence;
        if (m_listener->ownsClipboard()) return;
        if (m_listener->hasShellItems()) {
            beginChange(0);
            m_snapshotValid = false;
            return;
        }
        const bool files = m_listener->hasFiles();
        std::string text;
        if (!files) {
            const auto nativeText = m_listener->readText(MaxBytes);
            if (nativeText) text = toUtf8(*nativeText);
        }
        auto pngBytes = m_listener->readPng(MaxBytes);
        auto image = WindowsClipboardPng::decode({pngBytes.data(), pngBytes.size()}, MaxPixels,
                                                 WindowsClipboardPng::Metadata::NativeClipboard);
        if (image.rgba.empty()) {
            pngBytes.clear();
            using Format = WindowsClipboardListener::BitmapFormat;
            for (auto format : {Format::DibV5, Format::Dib}) {
                const auto bmp = m_listener->readBitmap(format, MaxPixels * 4 + MaxBytes);
                if (bmp.empty()) continue;
                auto converted = WindowsClipboardPng::convertBitmap({bmp.data(), bmp.size()}, MaxPixels, MaxBytes);
                if (converted.png.empty()) continue;
                pngBytes = std::move(converted.png);
                image = std::move(converted.image);
                break;
            }
        }
        if (sequence != GetClipboardSequenceNumber()) {
            m_clipboardSequence = 0;
            return;
        }
        if (!files && pngBytes.empty() && m_listener->hasImage()) unsupported();
        const auto textHash = hash(text);
        const auto imageHash = hashImage(image);
        if (m_snapshotValid && textHash == m_textHash && imageHash == m_imageHash) return;
        beginChange(0);
        m_snapshotValid = true;
        m_textHash = textHash;
        m_imageHash = imageHash;
        if (pngBytes.empty()) {
            outbound(ClipboardFrame::Text, 0, std::move(text));
            return;
        }
        if (!text.empty() && text.size() <= ClipboardFrame::MaxInlinePayload) {
            m_outgoingToken = ++m_nextToken;
            if (!m_outgoingToken) m_outgoingToken = ++m_nextToken;
            outbound(ClipboardFrame::Text, m_outgoingToken, std::move(text));
        }
        outbound(ClipboardFrame::Png, m_outgoingToken, std::string(pngBytes.begin(), pngBytes.end()));
    }

    void apply(uint8_t kind, uint32_t token, std::string payload)
    {
        if (kind == ClipboardFrame::Text && fromUtf8(payload).empty()) return;
        if (token) {
            auto& part = kind == ClipboardFrame::Text ? m_burstText : m_burstPng;
            if (!part.empty()) return;
            part = std::move(payload);
        }
        const std::string_view text = token ? m_burstText : kind == ClipboardFrame::Text ? payload : std::string_view{};
        const std::string_view png = token ? m_burstPng : kind == ClipboardFrame::Png ? payload : std::string_view{};
        auto wide = fromUtf8(text);
        if (!png.empty()) {
            auto image = WindowsClipboardPng::decode(png, MaxPixels, WindowsClipboardPng::Metadata::NativeClipboard);
            if (!image.rgba.empty()) {
                for (size_t i = 0; i < image.rgba.size(); i += 4) std::swap(image.rgba[i], image.rgba[i + 2]);
                if (m_listener->writePng(png, image.rgba.data(), static_cast<int>(image.width),
                        static_cast<int>(image.height), image.width * 4,
                        image.dotsPerMeterX, image.dotsPerMeterY, wide) ==
                        WindowsClipboardListener::WriteResult::Written) m_snapshotValid = false;
                return;
            }
            unsupported();
            if (token) m_burstPng.clear();
        }
        if (!wide.empty() && m_listener->writeText(wide) == WindowsClipboardListener::WriteResult::Written) {
            m_snapshotValid = false;
        }
    }

    void incoming(std::string_view bytes)
    {
        const auto frame = ClipboardFrame::decode(bytes);
        if (!frame) return;
        localChanged();
        const auto token = frame->token;
        const auto kind = frame->kind;
        const auto now = Clock::now();
        while (!m_retired.empty() && now - m_retired.front().time > std::chrono::seconds(30)) m_retired.pop_front();
        if (token) {
            for (const auto& old : m_retired) if (old.token == token) return;
        }
        std::optional<NativeClipboardIpc::BlobReference> reference;
        if (kind == ClipboardFrame::Reference) {
            reference = NativeClipboardIpc::decodeReference(frame->payload);
            if (!reference || reference->id == m_outgoingReference) return;
            if (reference->mime != "image/png" && reference->mime.compare(0, 5, "text/") != 0) {
                unsupported();
                return;
            }
        } else if (kind != ClipboardFrame::Text && kind != ClipboardFrame::Png) return;
        if (m_snapshotValid && token == m_outgoingToken) {
            if (kind == ClipboardFrame::Text && hash(frame->payload) == m_textHash) return;
            if (kind == ClipboardFrame::Png) {
                const auto image = WindowsClipboardPng::decode(frame->payload, MaxPixels,
                    WindowsClipboardPng::Metadata::NativeClipboard);
                if (!image.rgba.empty() && hashImage(image) == m_imageHash) return;
            }
        }
        if (!token || token != m_currentToken) beginChange(token);
        else if (now > m_burstDeadline && m_transfers.empty()) {
            m_burstText.clear();
            m_burstPng.clear();
            m_burstDeadline = now + std::chrono::seconds(5);
        }
        if (reference) {
            const auto flavor = reference->mime == "image/png" ? ClipboardFrame::Png : ClipboardFrame::Text;
            request("/" + reference->id, flavor, {}, ClipboardBlob::downloadLimit(reference->size),
                [this, flavor, token](std::string data) { apply(flavor, token, std::move(data)); });
        } else apply(kind, token, std::string(frame->payload));
    }

    std::unique_ptr<WindowsClipboardListener> m_listener;
    NativeHttp::ClipboardCredentials m_credentials;
    std::string m_endpoint;
    std::unordered_map<uint64_t, std::unique_ptr<NativeHttp::WindowsTransfer>> m_transfers;
    std::deque<Retired> m_retired;
    uint64_t m_nextTransfer = 0;
    uint32_t m_nextSequence = 1;
    uint32_t m_nextToken = static_cast<uint32_t>(GetTickCount64() ^ GetCurrentProcessId());
    uint32_t m_currentToken = 0;
    uint32_t m_outgoingToken = 0;
    std::string m_outgoingReference;
    DWORD m_clipboardSequence = 0;
    std::string m_burstText;
    std::string m_burstPng;
    Clock::time_point m_burstDeadline{};
    uint64_t m_textHash = 0;
    uint64_t m_imageHash = 0;
    bool m_snapshotValid = false;
    bool m_outputFailed = false;
};
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    const auto inputReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!inputReady) return 1;
    std::mutex mutex;
    std::condition_variable consumed;
    std::string line;
    ClipboardPipe::LineStatus status{};
    bool pending = false;
    bool stopping = false;
    // Only one bounded record can be queued while a clipboard provider runs.
    std::thread input([&] {
        for (;;) {
            const auto next = ClipboardPipe::readLine(std::cin, line);
            std::unique_lock lock(mutex);
            if (stopping) break;
            status = next;
            pending = true;
            SetEvent(inputReady);
            consumed.wait(lock, [&] { return !pending || stopping; });
            if (stopping || next == ClipboardPipe::LineStatus::Incomplete) break;
        }
    });
    ClipboardHelper helper;
    bool running = true;
    while (running && !helper.outputFailed()) {
        const auto ready = MsgWaitForMultipleObjects(1, &inputReady, FALSE, INFINITE, QS_ALLINPUT);
        if (ready == WAIT_OBJECT_0) {
            std::unique_lock lock(mutex);
            const auto current = status;
            lock.unlock();
            // The reader waits for pending to clear before modifying the line.
            running = helper.handle(current, line);
            {
                std::lock_guard lock(mutex);
                pending = false;
                stopping = !running;
            }
            consumed.notify_one();
        } else if (ready == WAIT_OBJECT_0 + 1) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    running = false;
                    break;
                }
                DispatchMessageW(&message);
                if (helper.outputFailed()) break;
            }
        } else break;
    }
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    consumed.notify_one();
    // A pipe read may start just as the owner shuts down. Keep cancelling until
    // the reader has observed shutdown or completed that read.
    const auto thread = static_cast<HANDLE>(input.native_handle());
    while (WaitForSingleObject(thread, 10) == WAIT_TIMEOUT) CancelSynchronousIo(thread);
    input.join();
    CloseHandle(inputReady);
    return 0;
}
