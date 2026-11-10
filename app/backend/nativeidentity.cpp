#include "nativeidentity.h"
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>
#include <charconv>
#include <climits>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace NativeIdentity {
namespace {
template<class T, auto Destroy> using Owned = std::unique_ptr<T, decltype(Destroy)>;
void require(bool success) {
    if (!success) throw std::runtime_error("Identity cryptography failed");
}
std::string pem(BIO* bio) {
    BUF_MEM* bytes = nullptr;
    BIO_get_mem_ptr(bio, &bytes);
    require(bytes && bytes->data && bytes->length > 0);
    return {bytes->data, bytes->length};
}
}
Credentials generateCredentials()
{
    Owned<X509, X509_free> cert(X509_new(), X509_free);
    require(bool(cert));
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    Owned<EVP_PKEY, EVP_PKEY_free> key(EVP_RSA_gen(2048), EVP_PKEY_free);
#else
    Owned<EVP_PKEY_CTX, EVP_PKEY_CTX_free> context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    require(bool(context));
    require(EVP_PKEY_keygen_init(context.get()) == 1);
    require(EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) == 1);
    EVP_PKEY* generated = nullptr;
    const int result = EVP_PKEY_keygen(context.get(), &generated);
    Owned<EVP_PKEY, EVP_PKEY_free> key(generated, EVP_PKEY_free);
    require(result == 1);
#endif
    require(bool(key));
    require(X509_set_version(cert.get(), 2) == 1);
    require(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 0) == 1);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    require(X509_gmtime_adj(X509_get_notBefore(cert.get()), 0) != nullptr);
    require(X509_gmtime_adj(X509_get_notAfter(cert.get()), 60L * 60 * 24 * 365 * 20) != nullptr);
#else
    require(X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0) != nullptr);
    require(X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60L * 60 * 24 * 365 * 20) != nullptr);
#endif
    require(X509_set_pubkey(cert.get(), key.get()) == 1);
    Owned<X509_NAME, X509_NAME_free> name(X509_NAME_new(), X509_NAME_free);
    require(bool(name));
    const unsigned char commonName[] = "NVIDIA GameStream Client";
    require(X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC, commonName, -1, -1, 0) == 1);
    require(X509_set_subject_name(cert.get(), name.get()) == 1);
    require(X509_set_issuer_name(cert.get(), name.get()) == 1);
    require(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0);

    Owned<BIO, BIO_free> certificate(BIO_new(BIO_s_mem()), BIO_free);
    Owned<BIO, BIO_free> privateKey(BIO_new(BIO_s_mem()), BIO_free);
    require(certificate && privateKey);
    require(PEM_write_bio_X509(certificate.get(), cert.get()) == 1);
    require(PEM_write_bio_PrivateKey(privateKey.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1);
    return {pem(certificate.get()), pem(privateKey.get())};
}
std::string generateUniqueId()
{
    uint64_t value = 0;
    require(RAND_bytes(reinterpret_cast<unsigned char*>(&value), sizeof(value)) == 1);
    char buffer[16];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
    require(result.ec == std::errc());
    return {buffer, result.ptr};
}
std::string normalizePrivateKey(const std::string& input, bool traditional)
{
    if (input.empty() || input.size() > INT_MAX) return {};
    Owned<BIO, BIO_free> source(BIO_new_mem_buf(const_cast<char*>(input.data()), static_cast<int>(input.size())), BIO_free);
    require(bool(source));
    auto noPassword = [](char*, int, int, void*) -> int { return 0; };
    Owned<EVP_PKEY, EVP_PKEY_free> key(PEM_read_bio_PrivateKey(source.get(), nullptr, noPassword, nullptr), EVP_PKEY_free);
    if (!key) return {};
    Owned<BIO, BIO_free> output(BIO_new(BIO_s_mem()), BIO_free);
    require(bool(output));
    const int result = traditional
        ? PEM_write_bio_PrivateKey_traditional(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr)
        : PEM_write_bio_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr);
    if (result != 1) return {};
    return pem(output.get());
}
}
