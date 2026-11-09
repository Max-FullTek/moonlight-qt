// Capability probe only: not linked into the product or enabled by default.
#include <curl/curl.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <cstdio>
#include <memory>
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
struct Request {
    std::vector<unsigned char> pin;
    bool verified = false;
    bool oversized = false;
    size_t bytes = 0;
};
int verifyCertificate(X509_STORE_CTX* chain, void* opaque)
{
    auto& request = *static_cast<Request*>(opaque);
    try {
        const auto peer = der(X509_STORE_CTX_get0_cert(chain));
        request.verified = !request.pin.empty() && peer == request.pin;
    }
    catch (...) { request.verified = false; }
    X509_STORE_CTX_set_error(chain, request.verified ? X509_V_OK : X509_V_ERR_CERT_REJECTED);
    return request.verified ? 1 : 0;
}
CURLcode configureTls(CURL*, void* context, void* opaque)
{
    // Preserve the paired-certificate trust model: compare the complete leaf
    // DER during the handshake, including normally trusted certificates.
    SSL_CTX_set_cert_verify_callback(static_cast<SSL_CTX*>(context), verifyCertificate, opaque);
    return CURLE_OK;
}
size_t receive(char*, size_t size, size_t count, void* opaque)
{
    auto& request = *static_cast<Request*>(opaque);
    if (size && count > (64 - request.bytes) / size) {
        request.oversized = true;
        return 0;
    }
    request.bytes += size * count;
    return size * count;
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
        if (argc != 5) throw std::runtime_error("usage: native_host_http URL pinned-cert fixtures client-identity(0/1)");
        auto pin = certificate(argv[2]);
        Request request{der(pin.get())};
        if (request.pin.empty()) throw std::runtime_error("Empty certificate pin");
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("curl initialization failed");
        {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
            if (!curl) throw std::runtime_error("Cannot create curl request");
            const auto* version = curl_version_info(CURLVERSION_NOW);
            if (!version->ssl_version || std::string(version->ssl_version).find("OpenSSL/3.6.5") != 0) {
                throw std::runtime_error("Probe must use the repository's OpenSSL 3.6.5");
            }
            auto set = [&](CURLoption option, auto value) {
                if (curl_easy_setopt(curl.get(), option, value) != CURLE_OK) throw std::runtime_error("curl option rejected");
            };
            const std::string cert = std::string(argv[3]) + "/server.pem";
            const std::string key = std::string(argv[3]) + "/server-key.pem";
            set(CURLOPT_URL, argv[1]);
            set(CURLOPT_PROTOCOLS_STR, "https");
            set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
            set(CURLOPT_PROXY, "");
            set(CURLOPT_FOLLOWLOCATION, 0L);
            set(CURLOPT_TIMEOUT_MS, 700L);
            set(CURLOPT_FRESH_CONNECT, 1L);
            set(CURLOPT_FORBID_REUSE, 1L);
            set(CURLOPT_SSL_SESSIONID_CACHE, 0L);
            set(CURLOPT_SSL_VERIFYPEER, 1L);
            // Peer names/CA expiry are not the identity for a paired host.
            // No HTTP data may pass until the exact pinned leaf is accepted.
            set(CURLOPT_SSL_VERIFYHOST, 0L);
            set(CURLOPT_CAINFO, cert.c_str());
            set(CURLOPT_SSL_CTX_FUNCTION, configureTls);
            set(CURLOPT_SSL_CTX_DATA, &request);
            if (std::string(argv[4]) == "1") {
                set(CURLOPT_SSLCERT, cert.c_str());
                set(CURLOPT_SSLKEY, key.c_str());
            }
            set(CURLOPT_WRITEFUNCTION, receive);
            set(CURLOPT_WRITEDATA, &request);
            const auto result = curl_easy_perform(curl.get());
            long status = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
            std::printf("code=%d status=%ld verified=%d oversized=%d bytes=%zu\n",
                        static_cast<int>(result), status, request.verified, request.oversized, request.bytes);
        }
        curl_global_cleanup();
        return 0; // Python asserts outcomes and server-side request counts.
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
