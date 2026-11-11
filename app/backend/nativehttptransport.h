#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace NativeHttp {
struct Request;
struct Response;
Response perform(const Request& request);

// Monotonic, thread-safe cancellation. One active request per token. Cancel
// wakes the network wait; this object does not create a thread or a timer.
class Cancellation {
public:
    Cancellation();
    ~Cancellation();
    Cancellation(const Cancellation&) = delete;
    Cancellation& operator=(const Cancellation&) = delete;
    void cancel();
    bool requested() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
    friend Response perform(const Request& request);
    friend struct TransportDriver;
};

enum class Method { Get, Post };
enum class ClipboardMime { None, Text, Png };
enum class Error {
    None, InvalidRequest, Setup, Transport, Timeout, Cancelled,
    ResponseTooLarge, CertificateMismatch, Storage
};
struct Request {
    std::string url;
    Method method = Method::Get;
    std::string contentType;
    ClipboardMime clipboardMime = ClipboardMime::None;
    std::string body;
    std::vector<unsigned char> pinnedCertificateDer;
    std::string clientCertificatePem;
    std::string clientPrivateKeyPem;
    long timeoutMs = 5000; // Zero preserves an explicitly unlimited deadline.
    size_t maxResponseBytes = 0; // Zero leaves response policy to the owner.
    bool limitDeclaredLength = false; // Also reject an oversized Content-Length before the body arrives.
    bool allowUnpairedHttp = false;
    std::shared_ptr<Cancellation> cancellation;
};
struct Response {
    Error error = Error::None;
    int transportCode = 0;
    long httpStatus = 0;
    bool certificateVerified = false;
    std::string body;
    std::string message; // Generic error only; never a URL, key or response body.
};

struct SocketInterest {
    std::uintptr_t socket;
    bool read;
    bool write;
};
struct WaitState {
    std::vector<SocketInterest> sockets;
    long timeoutMs = -1; // -1 removes the timer; zero schedules immediate work.
};
struct ReadyEvent {
    enum class Kind { Socket, Timeout, Cancel };
    Kind kind = Kind::Cancel;
    std::uintptr_t socket = 0;
    bool read = false;
    bool write = false;
    bool error = false;
};

// A single transfer driven by socket and timer events on its owning thread.
// Begin with a Timeout event. Destroying the request cancels it immediately.
class AsyncRequest {
public:
    explicit AsyncRequest(Request request);
    ~AsyncRequest();
    AsyncRequest(const AsyncRequest&) = delete;
    AsyncRequest& operator=(const AsyncRequest&) = delete;
    WaitState waitState() const;
    // A returned response is terminal. Never drive a terminal request again.
    std::optional<Response> advance(ReadyEvent event);
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
// Owner-thread integration, not a thread or event dispatcher implementation.
// Wait for one requested socket/deadline or owner shutdown. Never call back
// into the transport from wait(). Cancellation is returned as Kind::Cancel;
// the cross-thread Cancellation token belongs only to the blocking overload.
class EventLoop {
public:
    virtual ~EventLoop() = default;
    virtual ReadyEvent wait(const WaitState& state) = 0;
};

// Runs on the caller's chosen thread. No retries, redirects, proxy lookup,
// credential files, connection reuse, UI dispatch or worker ownership here.
// HTTP status (including 3xx/4xx/5xx) is returned for the owner's protocol policy.
Response perform(const Request& request);
// Same request/TLS/error policy, driven by the owner's existing event loop.
// A non-null Request::cancellation is rejected; return Cancel from wait instead.
Response perform(const Request& request, EventLoop& eventLoop);
}
