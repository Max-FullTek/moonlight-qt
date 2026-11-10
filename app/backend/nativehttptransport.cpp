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
struct MultiEntry {
    CURLM* multi;
    CURL* easy;
    ~MultiEntry() { curl_multi_remove_handle(multi, easy); }
};
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

struct TransportDriver {
    static Response perform(const Request& request, EventLoop* eventLoop);
};
Response TransportDriver::perform(const Request& request, EventLoop* eventLoop)
{
    Response response;
    try {
        if (eventLoop && request.cancellation) throw std::invalid_argument("Event-loop requests use owner cancellation");
        auto cancellation = request.cancellation;
        if (cancellation && cancellation->requested()) {
            response.error = Error::Cancelled;
            response.transportCode = CURLE_ABORTED_BY_CALLBACK;
            response.message = "Request cancelled";
            return response;
        }
        static Runtime runtime;
        const bool https = httpsUrl(request);
        if (request.timeoutMs < 0 || request.contentType.find_first_of("\r\n") != std::string::npos ||
            request.contentType.find('\0') != std::string::npos ||
            (request.clientCertificatePem.empty() != request.clientPrivateKeyPem.empty()) ||
            (!https && (!request.pinnedCertificateDer.empty() || !request.clientCertificatePem.empty())) ||
            (request.method != Method::Get && request.method != Method::Post)) {
            throw std::invalid_argument("Invalid HTTP request policy");
        }
        std::string caPem = https ? pemPin(request.pinnedCertificateDer) : std::string{};
        Reactor reactor; // Must outlive multi cleanup, which may invoke callbacks.
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
        std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi(curl_multi_init(), curl_multi_cleanup);
        if (!easy || !multi) throw std::runtime_error("Cannot allocate HTTP request");
        Transfer transfer{request, response};
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
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
        if (!request.contentType.empty()) {
            headers.reset(curl_slist_append(nullptr, ("Content-Type: " + request.contentType).c_str()));
            if (!headers) throw std::bad_alloc();
            set(CURLOPT_HTTPHEADER, headers.get());
        }
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
        MultiEntry entry{multi.get(), easy.get()};
        std::unique_ptr<Cancellation::Impl::Registration> registration;
        if (cancellation) registration = std::make_unique<Cancellation::Impl::Registration>(*cancellation->m_Impl, multi.get());
        CURLcode result = CURLE_OK;
        bool cancelled = false;
        ReadyEvent ready{ReadyEvent::Kind::Timeout}; // Start outside callbacks.
        for (;;) {
            if (cancellation && cancellation->requested()) {
                cancelled = true;
                result = CURLE_ABORTED_BY_CALLBACK;
                break;
            }
            int running = 0;
            CURLMcode step;
            if (eventLoop) {
                const int events = (ready.read ? CURL_CSELECT_IN : 0) | (ready.write ? CURL_CSELECT_OUT : 0) |
                                   (ready.error ? CURL_CSELECT_ERR : 0);
                step = curl_multi_socket_action(multi.get(), ready.kind == ReadyEvent::Kind::Timeout ?
                                               CURL_SOCKET_TIMEOUT : static_cast<curl_socket_t>(ready.socket), events, &running);
            }
            else step = curl_multi_perform(multi.get(), &running);
            if (reactor.storageFailure) throw std::bad_alloc();
            if (step != CURLM_OK) throw std::runtime_error("HTTP transfer failed");
            int messages = 0;
            CURLMsg* message = curl_multi_info_read(multi.get(), &messages);
            if (message && message->msg == CURLMSG_DONE) { result = message->data.result; break; }
            if (!running) throw std::runtime_error("HTTP transfer ended without a result");
            if (eventLoop) {
                const auto state = reactor.state();
                ready = eventLoop->wait(state);
                if (ready.kind == ReadyEvent::Kind::Cancel) {
                    cancelled = true;
                    result = CURLE_ABORTED_BY_CALLBACK;
                    break;
                }
                if (ready.kind != ReadyEvent::Kind::Timeout && ready.kind != ReadyEvent::Kind::Socket) {
                    throw std::invalid_argument("Invalid HTTP readiness event");
                }
                if (ready.kind == ReadyEvent::Kind::Socket &&
                    reactor.sockets.find(static_cast<curl_socket_t>(ready.socket)) == reactor.sockets.end()) {
                    throw std::invalid_argument("Unrequested HTTP socket event");
                }
            }
            // Blocking owners sleep until socket activity, curl's own deadline
            // or cancellation. Neither driver uses fixed-interval polling.
            else if (curl_multi_poll(multi.get(), nullptr, 0, INT_MAX, nullptr) != CURLM_OK) {
                throw std::runtime_error("HTTP event wait failed");
            }
        }
        response.transportCode = result;
        curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &response.httpStatus);
        if (cancelled) response.error = Error::Cancelled;
        else if (transfer.storageFailure) response.error = Error::Storage;
        else if (transfer.oversized) response.error = Error::ResponseTooLarge;
        else if (transfer.mismatch) response.error = Error::CertificateMismatch;
        else if (result == CURLE_OPERATION_TIMEDOUT) response.error = Error::Timeout;
        else if (result != CURLE_OK) response.error = Error::Transport;
        if (response.error != Error::None) {
            response.body.clear();
            response.message = curl_easy_strerror(result);
        }
    }
    catch (const std::invalid_argument& error) { response.error = Error::InvalidRequest; response.message = error.what(); response.body.clear(); }
    catch (const std::bad_alloc&) { response.error = Error::Storage; response.message = "HTTP storage allocation failed"; response.body.clear(); }
    catch (const std::exception& error) { response.error = Error::Setup; response.message = error.what(); response.body.clear(); }
    return response;
}
Response perform(const Request& request) { return TransportDriver::perform(request, nullptr); }
Response perform(const Request& request, EventLoop& eventLoop) { return TransportDriver::perform(request, &eventLoop); }
}
