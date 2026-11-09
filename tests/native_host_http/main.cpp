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
int main(int argc, char** argv)
{
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
        const std::string mode = argc == 6 ? argv[5] : "";
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
        std::thread cancel;
        if (mode == "cancel") {
            request.timeoutMs = 0;
            cancel = std::thread([token = request.cancellation] { std::cin.get(); token->cancel(); });
        }
        auto response = NativeHttp::perform(request);
        if (cancel.joinable()) cancel.join();
        if (mode == "replace-pin") {
            auto other = certificate((std::string(argv[3]) + "/other.pem").c_str());
            if (response.error != NativeHttp::Error::None) throw std::runtime_error("First identity request failed");
            request.pinnedCertificateDer = der(other.get());
            response = NativeHttp::perform(request);
        }
        if (mode == "remove-identity") {
            if (response.error != NativeHttp::Error::None) throw std::runtime_error("First client identity request failed");
            request.clientCertificatePem.clear();
            request.clientPrivateKeyPem.clear();
            response = NativeHttp::perform(request);
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
