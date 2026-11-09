#pragma once

#include <cstddef>
#include <memory>
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
};

enum class Method { Get, Post };
enum class Error {
    None, InvalidRequest, Setup, Transport, Timeout, Cancelled,
    ResponseTooLarge, CertificateMismatch, Storage
};
struct Request {
    std::string url;
    Method method = Method::Get;
    std::string contentType;
    std::string body;
    std::vector<unsigned char> pinnedCertificateDer;
    std::string clientCertificatePem;
    std::string clientPrivateKeyPem;
    long timeoutMs = 5000; // Zero preserves an explicitly unlimited deadline.
    size_t maxResponseBytes = 0; // Zero leaves response policy to the owner.
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

// Runs on the caller's chosen thread. No retries, redirects, proxy lookup,
// credential files, connection reuse, UI dispatch or worker ownership here.
// HTTP status (including 3xx/4xx/5xx) is returned for the owner's protocol policy.
Response perform(const Request& request);
}
