// Exercise the native transport with public fixtures; not linked into the Qt worker.
#include "backend/nativehttptransport.h"
#include <curl/curl.h>
#include <iostream>
#include <thread>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <cstdio>
#include <memory>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#ifdef NATIVE_HTTP_QT_OWNER
#include "backend/qtnativehttptransport.h"
#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <exception>
#endif

#if LIBCURL_VERSION_NUM != 0x081700
#error This probe is pinned to curl 8.23.0
#endif

using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;
Certificate certificate(const char* path)
{
    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_file(path, "rb"), BIO_free);
    Certificate cert(input ? PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
    if (!cert) throw std::runtime_error("Cannot read test certificate");
    return cert;
}
std::vector<unsigned char> der(X509* cert)
{
    if (!cert) return {};
    const int length = i2d_X509(cert, nullptr);
    if (length <= 0) return {};
    std::vector<unsigned char> bytes(length);
    unsigned char* output = bytes.data();
    if (i2d_X509(cert, &output) != length) return {};
    return bytes;
}
void renewFixture(const std::string& fixtures, const char* output)
{
    auto cert = certificate((fixtures + "/server.pem").c_str());
    std::unique_ptr<BIO, decltype(&BIO_free)> keyInput(BIO_new_file((fixtures + "/server-key.pem").c_str(), "rb"), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        keyInput ? PEM_read_bio_PrivateKey(keyInput.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> target(BIO_new_file(output, "wb"), BIO_free);
    if (!key || !target || !ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 2) ||
        !X509_sign(cert.get(), key.get(), EVP_sha256()) || !PEM_write_bio_X509(target.get(), cert.get())) {
        throw std::runtime_error("Cannot generate same-key renewed test certificate");
    }
}
class SocketOwnerLoop final : public NativeHttp::EventLoop {
public:
    bool cancelAfterDeadline = false;
    unsigned waits = 0;
    std::chrono::steady_clock::time_point cancelAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    NativeHttp::ReadyEvent wait(const NativeHttp::WaitState& state) override
    {
        if (++waits > 100) throw std::runtime_error("Owner event loop is spinning");
        long timeout = state.timeoutMs;
        if (cancelAfterDeadline) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(cancelAt - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) return {};
            if (timeout < 0 || timeout > remaining) timeout = static_cast<long>(remaining);
        }
        std::vector<WSAPOLLFD> sockets;
        for (const auto& socket : state.sockets) {
            sockets.push_back({static_cast<SOCKET>(socket.socket), static_cast<SHORT>(
                (socket.read ? POLLRDNORM : 0) | (socket.write ? POLLWRNORM : 0)), 0});
        }
        if (sockets.empty()) {
            if (timeout < 0) throw std::runtime_error("No socket or deadline to wait for");
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        }
        else {
            const int result = WSAPoll(sockets.data(), static_cast<ULONG>(sockets.size()), timeout);
            if (result == SOCKET_ERROR) throw std::runtime_error("Test socket wait failed");
            for (const auto& socket : sockets) {
                if (socket.revents) return {NativeHttp::ReadyEvent::Kind::Socket, static_cast<std::uintptr_t>(socket.fd),
                    (socket.revents & (POLLRDNORM | POLLHUP)) != 0, (socket.revents & POLLWRNORM) != 0,
                    (socket.revents & (POLLERR | POLLNVAL)) != 0};
            }
        }
        return {NativeHttp::ReadyEvent::Kind::Timeout};
    }
};
NativeHttp::Response performRequest(NativeHttp::Request request, const std::string& mode, bool eventLoop)
{
#ifdef NATIVE_HTTP_QT_OWNER
    request.cancellation.reset();
    NativeHttp::Response response;
    std::exception_ptr failure;
    std::unique_ptr<QThread> worker;
    auto run = [&] {
        bool dispatched = false;
        QTimer progress;
        progress.setSingleShot(true);
        QObject::connect(&progress, &QTimer::timeout, [&] { dispatched = true; });
        QTimer closing;
        closing.setSingleShot(true);
        QObject::connect(&closing, &QTimer::timeout, QCoreApplication::instance(), &QCoreApplication::quit);
        progress.start(50);
        if (mode == "owner-cancel") closing.start(200);
        response = NativeHttp::performWithQtEvents(request);
        if ((mode == "owner-cancel" || request.url.find("/slow") != std::string::npos ||
             request.url.find("/partial") != std::string::npos) && !dispatched) {
            throw std::runtime_error("HTTP wait blocked the existing event dispatcher");
        }
    };
    QTimer::singleShot(0, QCoreApplication::instance(), [&] {
        auto guarded = [&] {
            try { run(); }
            catch (...) { failure = std::current_exception(); }
            QCoreApplication::quit();
        };
        if (qEnvironmentVariableIsSet("NATIVE_HTTP_QT_WORKER")) {
            worker.reset(QThread::create(guarded));
            worker->start(); // Test-owned caller, never created by the transport.
        }
        else guarded();
    });
    QCoreApplication::exec();
    if (worker && !worker->wait(3000)) throw std::runtime_error("HTTP owner did not exit after application shutdown");
    if (failure) std::rethrow_exception(failure);
    (void)eventLoop;
    return response;
#else
    if (!eventLoop) return NativeHttp::perform(request);
    request.cancellation.reset();
    SocketOwnerLoop loop;
    loop.cancelAfterDeadline = mode == "owner-cancel";
    return NativeHttp::perform(request, loop);
#endif
}
int main(int argc, char** argv)
{
#ifdef NATIVE_HTTP_QT_OWNER
    QCoreApplication application(argc, argv);
#endif
    try {
        if (argc == 4 && std::string(argv[1]) == "--renew-fixture") {
            renewFixture(argv[2], argv[3]);
            return 0;
        }
        if (argc != 5 && argc != 6) throw std::runtime_error("usage: native_host_http URL pinned-cert fixtures client-identity(0/1) [mode]");
        auto pin = certificate(argv[2]);
        NativeHttp::Request request;
        request.url = argv[1];
        request.pinnedCertificateDer = der(pin.get());
        request.timeoutMs = 700;
        request.maxResponseBytes = 64;
        auto readFile = [](const std::string& path) {
            std::ifstream input(path, std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        };
        if (std::string(argv[4]) == "1") {
            request.clientCertificatePem = readFile(std::string(argv[3]) + "/server.pem");
            request.clientPrivateKeyPem = readFile(std::string(argv[3]) + "/server-key.pem");
        }
        std::string mode = argc == 6 ? argv[5] : "";
        const bool eventLoop = mode.find("event-") == 0;
        if (eventLoop) mode.erase(0, 6);
        if (mode == "no-pin") request.pinnedCertificateDer.clear();
        if (mode == "invalid-pin") request.pinnedCertificateDer = {0};
        if (mode == "partial-identity") request.clientPrivateKeyPem.clear();
        if (mode == "exact-limit") request.maxResponseBytes = 2;
        if (mode == "unpaired" || mode == "unpaired-credentials") {
            request.allowUnpairedHttp = true;
            if (mode == "unpaired") request.pinnedCertificateDer.clear();
        }
        if (mode == "post") {
            request.method = NativeHttp::Method::Post;
            request.contentType = "application/json";
            request.body = "{\"message\":\"繁中\"}";
        }
        request.cancellation = std::make_shared<NativeHttp::Cancellation>();
        if (mode == "pre-cancel") request.cancellation->cancel();
        if (mode == "owner-cancel") request.timeoutMs = 0;
        std::thread cancel;
        if (mode == "cancel") {
            request.timeoutMs = 0;
            cancel = std::thread([token = request.cancellation] { std::cin.get(); token->cancel(); });
        }
        auto response = performRequest(request, mode, eventLoop);
        if (cancel.joinable()) cancel.join();
        if (mode == "replace-pin") {
            auto other = certificate((std::string(argv[3]) + "/other.pem").c_str());
            if (response.error != NativeHttp::Error::None) throw std::runtime_error("First identity request failed");
            request.pinnedCertificateDer = der(other.get());
            response = performRequest(request, mode, eventLoop);
        }
        if (mode == "remove-identity") {
            if (response.error != NativeHttp::Error::None) throw std::runtime_error("First client identity request failed");
            request.clientCertificatePem.clear();
            request.clientPrivateKeyPem.clear();
            response = performRequest(request, mode, eventLoop);
        }
        std::printf("code=%d status=%ld verified=%d oversized=%d bytes=%zu error=%d cancelled=%d\n",
                    response.transportCode, response.httpStatus, response.certificateVerified,
                    response.error == NativeHttp::Error::ResponseTooLarge, response.body.size(),
                    static_cast<int>(response.error), response.error == NativeHttp::Error::Cancelled);
        return 0; // Python asserts outcomes and server-side request counts.
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
