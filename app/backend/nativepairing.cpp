#include "nativepairing.h"
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <climits>
#include <memory>
#include <stdexcept>

namespace NativePairing {
namespace {
template<class T, auto Destroy> using Owned = std::unique_ptr<T, decltype(Destroy)>;
using Certificate = Owned<X509, X509_free>;
using Key = Owned<EVP_PKEY, EVP_PKEY_free>;
void freeDigest(EVP_MD_CTX* context) { EVP_MD_CTX_destroy(context); }
void require(bool success) { if (!success) throw std::runtime_error("Pairing cryptography failed"); }
Certificate certificate(const std::string& pem)
{
    if (pem.empty() || pem.size() > INT_MAX) return {nullptr, X509_free};
    Owned<BIO, BIO_free> bio(BIO_new_mem_buf(const_cast<char*>(pem.data()), static_cast<int>(pem.size())), BIO_free);
    if (!bio) throw std::bad_alloc();
    return {PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free};
}
Key privateKey(const std::string& pem)
{
    if (pem.empty() || pem.size() > INT_MAX) throw std::runtime_error("Unable to load private key");
    Owned<BIO, BIO_free> bio(BIO_new_mem_buf(const_cast<char*>(pem.data()), static_cast<int>(pem.size())), BIO_free);
    if (!bio) throw std::bad_alloc();
    Key key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!key) throw std::runtime_error("Unable to load private key");
    return key;
}
Bytes randomBytes()
{
    Bytes bytes(16);
    require(RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) == 1);
    return bytes;
}
void append(Bytes& target, const Bytes& data) { target.insert(target.end(), data.begin(), data.end()); }
Bytes signature(X509* cert)
{
    const ASN1_BIT_STRING* value = nullptr;
#if OPENSSL_VERSION_NUMBER < 0x10002000L
    value = cert->signature;
    const auto* data = ASN1_STRING_data(const_cast<ASN1_BIT_STRING*>(value));
#elif OPENSSL_VERSION_NUMBER < 0x10100000L
    ASN1_BIT_STRING* legacy = nullptr;
    X509_get0_signature(&legacy, nullptr, cert);
    value = legacy;
    const auto* data = ASN1_STRING_data(const_cast<ASN1_BIT_STRING*>(value));
#else
    X509_get0_signature(&value, nullptr, cert);
    const auto* data = ASN1_STRING_get0_data(value);
#endif
    const int size = ASN1_STRING_length(value);
    require(data && size > 0);
    return {data, data + size};
}
Bytes hash(const Bytes& input, const EVP_MD* digest)
{
    Bytes result(EVP_MAX_MD_SIZE);
    unsigned int length = 0;
    require(EVP_Digest(input.data(), input.size(), result.data(), &length, digest, nullptr) == 1);
    result.resize(length);
    return result;
}
Bytes cipher(const Bytes& input, const Bytes& key, bool encrypt)
{
    if (key.size() != 16 || input.size() % 16 || input.size() > INT_MAX - 16) return {};
    if (input.empty()) return {};
    Owned<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) throw std::bad_alloc();
    require(EVP_CipherInit_ex(context.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr, encrypt ? 1 : 0) == 1);
    require(EVP_CIPHER_CTX_set_padding(context.get(), 0) == 1);
    Bytes output(input.size() + 16);
    int length = 0, finalLength = 0;
    require(EVP_CipherUpdate(context.get(), output.data(), &length, input.data(), static_cast<int>(input.size())) == 1);
    require(EVP_CipherFinal_ex(context.get(), output.data() + length, &finalLength) == 1);
    output.resize(static_cast<size_t>(length) + finalLength);
    return output;
}
Bytes sign(const Bytes& input, EVP_PKEY* key)
{
    Owned<EVP_MD_CTX, freeDigest> context(EVP_MD_CTX_create(), freeDigest);
    if (!context) throw std::bad_alloc();
    require(EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key) == 1);
    require(EVP_DigestSignUpdate(context.get(), input.data(), input.size()) == 1);
    size_t length = 0;
    require(EVP_DigestSignFinal(context.get(), nullptr, &length) == 1);
    Bytes result(length);
    require(EVP_DigestSignFinal(context.get(), result.data(), &length) == 1);
    result.resize(length);
    return result;
}
bool verify(const Bytes& input, const Bytes& signedData, X509* cert)
{
    Key key(X509_get_pubkey(cert), EVP_PKEY_free);
    Owned<EVP_MD_CTX, freeDigest> context(EVP_MD_CTX_create(), freeDigest);
    if (!key || !context) throw std::bad_alloc();
    require(EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1);
    require(EVP_DigestVerifyUpdate(context.get(), input.data(), input.size()) == 1);
    return EVP_DigestVerifyFinal(context.get(), signedData.data(), signedData.size()) == 1;
}
std::string hex(const Bytes& bytes)
{
    const char alphabet[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto value : bytes) { result += alphabet[value >> 4]; result += alphabet[value & 15]; }
    return result;
}
}
Bytes deriveKey(const Bytes& salt, const std::string& pin, int serverMajorVersion)
{
    if (salt.size() != 16) throw std::invalid_argument("Pairing salt must contain 16 bytes");
    auto saltedPin = salt;
    saltedPin.insert(saltedPin.end(), pin.begin(), pin.end());
    auto result = hash(saltedPin, serverMajorVersion >= 7 ? EVP_sha256() : EVP_sha1());
    result.resize(16);
    return result;
}
State pair(Host& host, int serverMajorVersion, const std::string& pin,
           const std::string& clientCertificatePem, const std::string& clientPrivateKeyPem,
           std::string& certificatePem)
{
    auto client = certificate(clientCertificatePem);
    if (!client) throw std::runtime_error("Unable to load certificate");
    auto key = privateKey(clientPrivateKeyPem);
    const auto* digest = serverMajorVersion >= 7 ? EVP_sha256() : EVP_sha1();
    const size_t hashLength = serverMajorVersion >= 7 ? 32 : 20;
    auto salt = randomBytes();
    auto aesKey = deriveKey(salt, pin, serverMajorVersion);
    constexpr auto prefix = "devicename=roth&updateState=1&";
    auto response = host.exchange(false, std::string(prefix) + "phrase=getservercert&salt=" + hex(salt) +
        "&clientcert=" + hex(Bytes(clientCertificatePem.begin(), clientCertificatePem.end())),
        0, "pair stage 1 getservercert");
    if (!response.paired) return State::Failed;
    auto reject = [&](State state) { host.unpair(); return state; };
    if (response.certificatePem.empty()) return reject(State::AlreadyInProgress);
    const auto serverPem = response.certificatePem;
    auto server = certificate(serverPem);
    if (!server) return reject(State::Failed);
    if (!response.pairName.empty()) host.savePairName(response.pairName);
    if (!host.pinCertificate(serverPem)) return reject(State::Failed);

    const auto challenge = randomBytes();
    response = host.exchange(false, std::string(prefix) + "clientchallenge=" + hex(cipher(challenge, aesKey, true)),
                             5000, "pair stage 2 clientchallenge");
    if (!response.paired) return reject(State::Failed);
    const auto challengeData = cipher(response.challengeResponse, aesKey, false);
    // The response contains the hash AND the full 16-byte server challenge.
    if (challengeData.size() < hashLength + 16) return reject(State::Failed);
    const Bytes serverResponse(challengeData.begin(), challengeData.begin() + hashLength);
    auto clientSecret = randomBytes();
    Bytes answer(challengeData.begin() + hashLength, challengeData.begin() + hashLength + 16);
    append(answer, signature(client.get()));
    append(answer, clientSecret);
    auto paddedHash = hash(answer, digest);
    paddedHash.resize(32, 0);
    response = host.exchange(false, std::string(prefix) + "serverchallengeresp=" + hex(cipher(paddedHash, aesKey, true)),
                             5000, "pair stage 3 serverchallengeresp");
    if (!response.paired || response.pairingSecret.size() <= 16) return reject(State::Failed);
    const Bytes serverSecret(response.pairingSecret.begin(), response.pairingSecret.begin() + 16);
    const Bytes serverSignature(response.pairingSecret.begin() + 16, response.pairingSecret.end());
    if (!verify(serverSecret, serverSignature, server.get())) return reject(State::Failed);
    auto expected = challenge;
    append(expected, signature(server.get()));
    append(expected, serverSecret);
    const auto expectedHash = hash(expected, digest);
    if (CRYPTO_memcmp(expectedHash.data(), serverResponse.data(), hashLength)) return reject(State::PinWrong);

    auto clientPairingSecret = clientSecret;
    append(clientPairingSecret, sign(clientSecret, key.get()));
    response = host.exchange(false, std::string(prefix) + "clientpairingsecret=" + hex(clientPairingSecret),
                             5000, "pair stage 4 clientpairingsecret");
    if (!response.paired) return reject(State::Failed);
    response = host.exchange(true, std::string(prefix) + "phrase=pairchallenge", 5000, "pair stage 5 pairchallenge");
    if (!response.paired) return reject(State::Failed);
    certificatePem = serverPem;
    return State::Paired;
}
}
