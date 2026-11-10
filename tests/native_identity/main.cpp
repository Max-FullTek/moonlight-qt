#include "backend/nativeidentity.h"
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

void check(bool condition) { if (!condition) throw std::runtime_error("Identity contract failed"); }
int main()
{
    try {
        const auto first = NativeIdentity::generateCredentials();
        const auto second = NativeIdentity::generateCredentials();
        check(first.certificatePem != second.certificatePem && first.privateKeyPem != second.privateKeyPem);
        for (const auto* identity : {&first, &second}) {
            check(NativeIdentity::normalizePrivateKey(identity->privateKeyPem, false) == identity->privateKeyPem);
            const auto traditional = NativeIdentity::normalizePrivateKey(identity->privateKeyPem, true);
            check(traditional.find("-----BEGIN RSA PRIVATE KEY-----") == 0);
            check(NativeIdentity::normalizePrivateKey(traditional, false) == identity->privateKeyPem);
            check(identity->certificatePem.find("-----BEGIN CERTIFICATE-----") == 0);
            check(identity->privateKeyPem.find("-----BEGIN PRIVATE KEY-----") == 0);
            std::unique_ptr<BIO, decltype(&BIO_free)> certBio(BIO_new_mem_buf(identity->certificatePem.data(), static_cast<int>(identity->certificatePem.size())), BIO_free);
            std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(BIO_new_mem_buf(identity->privateKeyPem.data(), static_cast<int>(identity->privateKeyPem.size())), BIO_free);
            check(certBio && keyBio);
            std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr), X509_free);
            std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
            check(cert && key);
            std::unique_ptr<BIO, decltype(&BIO_free)> encrypted(BIO_new(BIO_s_mem()), BIO_free);
            check(bool(encrypted));
            unsigned char password[] = "fixture-password";
            check(PEM_write_bio_PrivateKey(encrypted.get(), key.get(), EVP_aes_256_cbc(), password,
                                          sizeof(password) - 1, nullptr, nullptr) == 1);
            BUF_MEM* protectedPem = nullptr;
            BIO_get_mem_ptr(encrypted.get(), &protectedPem);
            check(protectedPem && NativeIdentity::normalizePrivateKey(
                std::string(protectedPem->data, protectedPem->length), false).empty());
            check(EVP_PKEY_base_id(key.get()) == EVP_PKEY_RSA && EVP_PKEY_bits(key.get()) == 2048);
            check(X509_check_private_key(cert.get(), key.get()) == 1 && X509_verify(cert.get(), key.get()) == 1);
            check(X509_get_version(cert.get()) == 2 && ASN1_INTEGER_get(X509_get_serialNumber(cert.get())) == 0);
            check(X509_get_signature_nid(cert.get()) == NID_sha256WithRSAEncryption && X509_get_ext_count(cert.get()) == 0);
            check(X509_NAME_cmp(X509_get_subject_name(cert.get()), X509_get_issuer_name(cert.get())) == 0);
            char cn[128] = {};
            check(X509_NAME_get_text_by_NID(X509_get_subject_name(cert.get()), NID_commonName, cn, sizeof(cn)) > 0);
            check(std::string(cn) == "NVIDIA GameStream Client");
            int days = 0, seconds = 0;
            check(ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(cert.get()), X509_get0_notAfter(cert.get())) == 1);
            check(days == 365 * 20 && seconds == 0);
            check(X509_cmp_current_time(X509_get0_notBefore(cert.get())) <= 0);
            check(X509_cmp_current_time(X509_get0_notAfter(cert.get())) > 0);
        }
        for (const auto* invalid : {"", "not PEM", "-----BEGIN PRIVATE KEY-----\ninvalid\n-----END PRIVATE KEY-----"}) {
            check(NativeIdentity::normalizePrivateKey(invalid, false).empty());
            check(NativeIdentity::normalizePrivateKey(invalid, true).empty());
        }
        for (int i = 0; i < 16; ++i) {
            const auto uid = NativeIdentity::generateUniqueId();
            check(!uid.empty() && uid.size() <= 16 && (uid.size() == 1 || uid[0] != '0'));
            check(std::all_of(uid.begin(), uid.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
        }
        std::cout << "RSA-2048, X509 v3, self-signature, SHA256, legacy subject/serial/validity, PKCS8 PEM and hexadecimal ID: passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
