#include "nativehttptransport.h"
#include <curl/curl.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <climits>
#include <algorithm>
#include <chrono>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>

#if LIBCURL_VERSION_NUM != 0x081700
#error Native HTTP currently requires the verified curl 8.23.0 source
#endif

namespace NativeHttp {
struct Cancellation::Impl {
    mutable std::mutex lock;
    bool cancelled = false;
    CURLM* waiting = nullptr;
    struct Registration {
        Impl& state;
        Registration(Impl& state, CURLM* multi) : state(state)
        {
            std::lock_guard<std::mutex> guard(state.lock);
            if (state.waiting) throw std::invalid_argument("Cancellation token already in use");
            state.waiting = multi;
        }
        ~Registration()
        {
            std::lock_guard<std::mutex> guard(state.lock);
            state.waiting = nullptr;
        }
    };
};
Cancellation::Cancellation() : m_Impl(std::make_unique<Impl>()) {}
Cancellation::~Cancellation() = default;
void Cancellation::cancel()
{
    std::lock_guard<std::mutex> guard(m_Impl->lock);
    m_Impl->cancelled = true;
    if (m_Impl->waiting) curl_multi_wakeup(m_Impl->waiting);
}
bool Cancellation::requested() const
{
    std::lock_guard<std::mutex> guard(m_Impl->lock);
    return m_Impl->cancelled;
}

namespace {
struct Runtime {
    Runtime()
    {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("HTTP runtime initialization failed");
        const auto* info = curl_version_info(CURLVERSION_NOW);
        if (!info->ssl_version || std::string(info->ssl_version).find("OpenSSL/") != 0) {
            curl_global_cleanup();
            throw std::runtime_error("HTTP transport requires the OpenSSL backend");
        }
    }
    ~Runtime() { curl_global_cleanup(); }
};
struct Transfer {
    const Request& request;
    Response& response;
    bool mismatch = false;
    bool oversized = false;
    bool storageFailure = false;
};
std::vector<unsigned char> der(X509* cert)
{
    if (!cert) return {};
    const int length = i2d_X509(cert, nullptr);
    if (length <= 0) return {};
    std::vector<unsigned char> bytes(length);
    auto* output = bytes.data();
    if (i2d_X509(cert, &output) != length) return {};
    return bytes;
}
int verifyCertificate(X509_STORE_CTX* chain, void* opaque)
{
    auto& transfer = *static_cast<Transfer*>(opaque);
    try {
        const auto peer = der(X509_STORE_CTX_get0_cert(chain));
        transfer.response.certificateVerified = !peer.empty() && peer == transfer.request.pinnedCertificateDer;
        transfer.mismatch = !transfer.response.certificateVerified;
    }
    catch (...) { transfer.storageFailure = true; transfer.response.certificateVerified = false; }
    X509_STORE_CTX_set_error(chain, transfer.response.certificateVerified ? X509_V_OK : X509_V_ERR_CERT_REJECTED);
    return transfer.response.certificateVerified ? 1 : 0;
}
CURLcode configureTls(CURL*, void* context, void* opaque)
{
    SSL_CTX_set_cert_verify_callback(static_cast<SSL_CTX*>(context), verifyCertificate, opaque);
    return CURLE_OK;
}
size_t receive(char* data, size_t size, size_t count, void* opaque)
{
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (size && count > (std::numeric_limits<size_t>::max)() / size) {
        transfer.oversized = true;
        return 0;
    }
    const size_t bytes = size * count;
    const auto limit = transfer.request.maxResponseBytes;
    if (limit && bytes > limit - transfer.response.body.size()) {
        transfer.oversized = true;
        return 0;
    }
    try { transfer.response.body.append(data, bytes); }
    catch (...) { transfer.storageFailure = true; return 0; }
    return bytes;
}
bool httpsUrl(const Request& request)
{
    if (request.url.find('\0') != std::string::npos) throw std::invalid_argument("Invalid request URL");
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, request.url.c_str(), 0) != CURLUE_OK) {
        throw std::invalid_argument("Invalid request URL");
    }
    char* scheme = nullptr;
    if (curl_url_get(url.get(), CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK) throw std::invalid_argument("Missing URL scheme");
    std::unique_ptr<char, decltype(&curl_free)> ownedScheme(scheme, curl_free);
    const bool https = std::string(scheme) == "https";
    if (!https && !(std::string(scheme) == "http" && request.allowUnpairedHttp)) {
        throw std::invalid_argument("Request scheme is not permitted");
    }
    return https;
}
std::string pemPin(const std::vector<unsigned char>& pin)
{
    if (pin.empty() || pin.size() > LONG_MAX) throw std::invalid_argument("Missing or invalid paired certificate");
    const auto* input = pin.data();
    std::unique_ptr<X509, decltype(&X509_free)> cert(d2i_X509(nullptr, &input, static_cast<long>(pin.size())), X509_free);
    if (!cert || input != pin.data() + pin.size() || der(cert.get()) != pin) {
        throw std::invalid_argument("Invalid paired certificate DER");
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
    if (!output || !PEM_write_bio_X509(output.get(), cert.get())) throw std::runtime_error("Cannot prepare paired certificate");
    char* data = nullptr;
    const long length = BIO_get_mem_data(output.get(), &data);
    if (length <= 0) throw std::runtime_error("Cannot prepare paired certificate");
    return {data, static_cast<size_t>(length)};
}
// curl owns the socket set and its one-shot timer. Snapshot only between curl
// calls: callbacks must not dispatch events or recursively enter libcurl.
struct Reactor {
    std::map<curl_socket_t, int> sockets;
    bool hasDeadline = false;
    bool storageFailure = false;
    std::chrono::steady_clock::time_point deadline;
    static int socketChanged(CURL*, curl_socket_t socket, int interest, void* opaque, void*)
    {
        auto& self = *static_cast<Reactor*>(opaque);
        try {
            if (interest == CURL_POLL_REMOVE || interest == CURL_POLL_NONE) self.sockets.erase(socket);
            else self.sockets[socket] = interest;
            return 0;
        }
        catch (...) { self.storageFailure = true; return -1; }
    }
    static int timerChanged(CURLM*, long milliseconds, void* opaque)
    {
        auto& self = *static_cast<Reactor*>(opaque);
        self.hasDeadline = milliseconds >= 0;
        if (self.hasDeadline) self.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        return 0;
    }
    WaitState state() const
    {
        WaitState state;
        for (const auto& item : sockets) {
            state.sockets.push_back({static_cast<std::uintptr_t>(item.first),
                item.second == CURL_POLL_IN || item.second == CURL_POLL_INOUT,
                item.second == CURL_POLL_OUT || item.second == CURL_POLL_INOUT});
        }
        if (hasDeadline) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
            state.timeoutMs = milliseconds <= 0 ? 0 : static_cast<long>((std::min<long long>)(milliseconds, LONG_MAX));
        }
        return state;
    }
};
}

namespace {

struct TransferState {
    explicit TransferState(const Request& request, bool eventLoop)
        : transfer{request, response}
    {
        static Runtime runtime;
        const bool https = httpsUrl(request);
        const char* clipboardHeader = nullptr;
        switch (request.clipboardMime) {
        case ClipboardMime::None: break;
        case ClipboardMime::Text: clipboardHeader = "X-Clipboard-Mime: text/plain"; break;
        case ClipboardMime::Png: clipboardHeader = "X-Clipboard-Mime: image/png"; break;
        default: throw std::invalid_argument("Invalid clipboard MIME");
        }
        if (request.timeoutMs < 0 || request.contentType.find_first_of("\r\n") != std::string::npos ||
            request.contentType.find('\0') != std::string::npos ||
            (request.clientCertificatePem.empty() != request.clientPrivateKeyPem.empty()) ||
            (!https && (!request.pinnedCertificateDer.empty() || !request.clientCertificatePem.empty())) ||
            (clipboardHeader && (!https || request.method != Method::Post ||
                                 request.clientCertificatePem.empty() ||
                                 request.contentType != "application/octet-stream")) ||
            (request.method != Method::Get && request.method != Method::Post)) {
            throw std::invalid_argument("Invalid HTTP request policy");
        }
        caPem = https ? pemPin(request.pinnedCertificateDer) : std::string{};
        easy.reset(curl_easy_init());
        multi.reset(curl_multi_init());
        if (!easy || !multi) throw std::runtime_error("Cannot allocate HTTP request");
        auto set = [&](CURLoption option, auto value) {
            if (curl_easy_setopt(easy.get(), option, value) != CURLE_OK) throw std::runtime_error("HTTP option rejected");
        };
        set(CURLOPT_URL, request.url.c_str());
        set(CURLOPT_PROTOCOLS_STR, https ? "https" : "http");
        set(CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
        set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
        set(CURLOPT_PROXY, "");
        set(CURLOPT_FOLLOWLOCATION, 0L);
        set(CURLOPT_TIMEOUT_MS, request.timeoutMs);
        if (request.limitDeclaredLength && request.maxResponseBytes &&
                request.maxResponseBytes <= static_cast<size_t>((std::numeric_limits<curl_off_t>::max)())) {
            set(CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(request.maxResponseBytes));
        }
        set(CURLOPT_FRESH_CONNECT, 1L);
        set(CURLOPT_FORBID_REUSE, 1L);
        set(CURLOPT_SSL_SESSIONID_CACHE, 0L);
        curl_blob ca{caPem.data(), caPem.size(), CURL_BLOB_NOCOPY};
        curl_blob cert{const_cast<char*>(request.clientCertificatePem.data()), request.clientCertificatePem.size(), CURL_BLOB_NOCOPY};
        curl_blob key{const_cast<char*>(request.clientPrivateKeyPem.data()), request.clientPrivateKeyPem.size(), CURL_BLOB_NOCOPY};
        if (https) {
            set(CURLOPT_SSL_VERIFYPEER, 1L);
            set(CURLOPT_SSL_VERIFYHOST, 0L); // Full paired leaf is the identity.
            set(CURLOPT_CAINFO_BLOB, &ca);
            set(CURLOPT_SSL_CTX_FUNCTION, configureTls);
            set(CURLOPT_SSL_CTX_DATA, &transfer);
            if (!request.clientCertificatePem.empty()) {
                set(CURLOPT_SSLCERTTYPE, "PEM");
                set(CURLOPT_SSLCERT_BLOB, &cert);
                set(CURLOPT_SSLKEY_BLOB, &key);
            }
        }
        auto appendHeader = [&](const char* value) {
            auto* list = curl_slist_append(headers.get(), value);
            if (!list) throw std::bad_alloc();
            headers.release();
            headers.reset(list);
        };
        if (!request.contentType.empty()) appendHeader(("Content-Type: " + request.contentType).c_str());
        if (clipboardHeader) appendHeader(clipboardHeader);
        if (headers) set(CURLOPT_HTTPHEADER, headers.get());
        if (request.method == Method::Post) {
            if (request.body.size() > static_cast<size_t>((std::numeric_limits<curl_off_t>::max)())) {
                throw std::invalid_argument("Request body is too large");
            }
            set(CURLOPT_POSTFIELDS, request.body.data());
            set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        }
        set(CURLOPT_WRITEFUNCTION, receive);
        set(CURLOPT_WRITEDATA, &transfer);
        if (eventLoop &&
            (curl_multi_setopt(multi.get(), CURLMOPT_SOCKETFUNCTION, Reactor::socketChanged) != CURLM_OK ||
             curl_multi_setopt(multi.get(), CURLMOPT_SOCKETDATA, &reactor) != CURLM_OK ||
             curl_multi_setopt(multi.get(), CURLMOPT_TIMERFUNCTION, Reactor::timerChanged) != CURLM_OK ||
             curl_multi_setopt(multi.get(), CURLMOPT_TIMERDATA, &reactor) != CURLM_OK)) {
            throw std::runtime_error("HTTP event-loop setup failed");
        }
        if (curl_multi_add_handle(multi.get(), easy.get()) != CURLM_OK) throw std::runtime_error("Cannot start HTTP request");
        added = true;
    }

    ~TransferState()
    {
        if (added) curl_multi_remove_handle(multi.get(), easy.get());
    }

    std::optional<Response> advance(const ReadyEvent* ready)
    {
        if (ready && ready->kind == ReadyEvent::Kind::Cancel) {
            return finish(CURLE_ABORTED_BY_CALLBACK, true);
        }
        int running = 0;
        CURLMcode step;
        if (ready) {
            const int events = (ready->read ? CURL_CSELECT_IN : 0) |
                               (ready->write ? CURL_CSELECT_OUT : 0) |
                               (ready->error ? CURL_CSELECT_ERR : 0);
            curl_socket_t socket = CURL_SOCKET_TIMEOUT;
            if (ready->kind == ReadyEvent::Kind::Socket) {
                socket = static_cast<curl_socket_t>(ready->socket);
                if (reactor.sockets.find(socket) == reactor.sockets.end()) {
                    throw std::invalid_argument("Unrequested HTTP socket event");
                }
            } else if (ready->kind != ReadyEvent::Kind::Timeout) {
                throw std::invalid_argument("Invalid HTTP readiness event");
            }
            step = curl_multi_socket_action(multi.get(), socket, events, &running);
        } else {
            step = curl_multi_perform(multi.get(), &running);
        }
        if (reactor.storageFailure) throw std::bad_alloc();
        if (step != CURLM_OK) throw std::runtime_error("HTTP transfer failed");
        int messages = 0;
        const auto* message = curl_multi_info_read(multi.get(), &messages);
        if (message && message->msg == CURLMSG_DONE) return finish(message->data.result, false);
        if (!running) throw std::runtime_error("HTTP transfer ended without a result");
        return std::nullopt;
    }

    Response finish(CURLcode result, bool cancelled)
    {
        response.transportCode = result;
        curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &response.httpStatus);
        if (cancelled) response.error = Error::Cancelled;
        else if (transfer.storageFailure) response.error = Error::Storage;
        else if (transfer.oversized || result == CURLE_FILESIZE_EXCEEDED) response.error = Error::ResponseTooLarge;
        else if (transfer.mismatch) response.error = Error::CertificateMismatch;
        else if (result == CURLE_OPERATION_TIMEDOUT) response.error = Error::Timeout;
        else if (result != CURLE_OK) response.error = Error::Transport;
        if (response.error != Error::None) {
            response.body.clear();
            response.message = curl_easy_strerror(result);
        }
        return std::move(response);
    }

    // Keep callback state and request-owned data alive through curl cleanup.
    Reactor reactor;
    std::string caPem;
    Response response;
    Transfer transfer;
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers{nullptr, curl_slist_free_all};
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy{nullptr, curl_easy_cleanup};
    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi{nullptr, curl_multi_cleanup};
    bool added = false;
};

Response failureFromException()
{
    Response response;
    try { throw; }
    catch (const std::invalid_argument& error) { response.error = Error::InvalidRequest; response.message = error.what(); }
    catch (const std::bad_alloc&) { response.error = Error::Storage; response.message = "HTTP storage allocation failed"; }
    catch (const std::exception& error) { response.error = Error::Setup; response.message = error.what(); }
    return response;
}

}

struct AsyncRequest::Impl {
    explicit Impl(Request input) : request(std::move(input))
    {
        try {
            if (request.cancellation) throw std::invalid_argument("Event-loop requests use owner cancellation");
            state = std::make_unique<TransferState>(request, true);
        } catch (...) {
            failure = failureFromException();
        }
    }
    Request request;
    std::unique_ptr<TransferState> state;
    Response failure;
};

AsyncRequest::AsyncRequest(Request request) : m_Impl(std::make_unique<Impl>(std::move(request))) {}
AsyncRequest::~AsyncRequest() = default;
WaitState AsyncRequest::waitState() const
{
    return m_Impl->state ? m_Impl->state->reactor.state() : WaitState{{}, 0};
}
std::optional<Response> AsyncRequest::advance(ReadyEvent event)
{
    if (!m_Impl->state) return std::move(m_Impl->failure);
    try {
        return m_Impl->state->advance(&event);
    } catch (...) {
        return failureFromException();
    }
}

struct TransportDriver {
    static Response perform(const Request& request, EventLoop* eventLoop);
};
Response TransportDriver::perform(const Request& request, EventLoop* eventLoop)
{
    try {
        if (eventLoop && request.cancellation) throw std::invalid_argument("Event-loop requests use owner cancellation");
        const auto cancellation = request.cancellation;
        if (cancellation && cancellation->requested()) {
            return {Error::Cancelled, CURLE_ABORTED_BY_CALLBACK, 0, false, {}, "Request cancelled"};
        }
        TransferState state(request, eventLoop != nullptr);
        std::unique_ptr<Cancellation::Impl::Registration> registration;
        if (cancellation) {
            registration = std::make_unique<Cancellation::Impl::Registration>(*cancellation->m_Impl, state.multi.get());
        }
        ReadyEvent ready{ReadyEvent::Kind::Timeout};
        for (;;) {
            if (cancellation && cancellation->requested()) return state.finish(CURLE_ABORTED_BY_CALLBACK, true);
            if (auto response = state.advance(eventLoop ? &ready : nullptr)) return std::move(*response);
            if (eventLoop) {
                ready = eventLoop->wait(state.reactor.state());
            } else if (curl_multi_poll(state.multi.get(), nullptr, 0, INT_MAX, nullptr) != CURLM_OK) {
                throw std::runtime_error("HTTP event wait failed");
            }
        }
    } catch (...) {
        return failureFromException();
    }
}
Response perform(const Request& request) { return TransportDriver::perform(request, nullptr); }
Response perform(const Request& request, EventLoop& eventLoop) { return TransportDriver::perform(request, &eventLoop); }
}
