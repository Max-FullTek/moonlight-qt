#include "backend/nativepairing.h"
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>

using NativePairing::Bytes;
void check(bool condition) { if (!condition) throw std::runtime_error("Pairing contract failed"); }
std::string file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    check(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
std::string hex(const Bytes& data)
{
    const char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : data) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
Bytes unhex(const std::string& text)
{
    check(text.size() % 2 == 0);
    auto nibble = [](char value) { return value <= '9' ? value - '0' : value - 'a' + 10; };
    Bytes result;
    for (size_t i = 0; i < text.size(); i += 2) result.push_back(static_cast<unsigned char>((nibble(text[i]) << 4) | nibble(text[i + 1])));
    return result;
}
Bytes digest(const Bytes& bytes, int generation)
{
    Bytes result(32);
    unsigned int size = 0;
    check(EVP_Digest(bytes.data(), bytes.size(), result.data(), &size,
                    generation >= 7 ? EVP_sha256() : EVP_sha1(), nullptr) == 1);
    result.resize(size);
    return result;
}
void append(Bytes& target, const Bytes& source) { target.insert(target.end(), source.begin(), source.end()); }
// Independent peer primitives, deliberately not using production helpers.
Bytes crypt(const Bytes& bytes, const Bytes& key, bool encrypt)
{
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    check(ctx && EVP_CipherInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr, encrypt) == 1);
    check(EVP_CIPHER_CTX_set_padding(ctx.get(), 0) == 1);
    Bytes result(bytes.size() + 16);
    int count = 0, last = 0;
    check(EVP_CipherUpdate(ctx.get(), result.data(), &count, bytes.data(), static_cast<int>(bytes.size())) == 1);
    check(EVP_CipherFinal_ex(ctx.get(), result.data() + count, &last) == 1);
    result.resize(static_cast<size_t>(count) + last);
    return result;
}
struct Peer final : NativePairing::Host {
    int generation;
    int stage = 0, unpairs = 0, failStage = 0, throwStage = 0;
    bool busy = false, malformedCert = false, badPin = false, badSignature = false;
    bool shortChallenge = false, nonBlockChallenge = false, shortSecret = false, rejectPin = false;
    bool pinned = false;
    std::string savedName;
    std::string pem, keyPem;
    Bytes aesKey, challenge, responseHash;
    const Bytes serverSecret = Bytes(16, 0x37), serverChallenge = Bytes(16, 0xA5);
    std::unique_ptr<X509, decltype(&X509_free)> cert{nullptr, X509_free};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{nullptr, EVP_PKEY_free};
    Peer(int generation, const std::string& pem, const std::string& keyPem) : generation(generation), pem(pem), keyPem(keyPem)
    {
        std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
        cert.reset(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr));
        input.reset(BIO_new_mem_buf(keyPem.data(), static_cast<int>(keyPem.size())));
        key.reset(PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr));
        check(cert && key);
    }
    Bytes certSignature()
    {
        const ASN1_BIT_STRING* signature = nullptr;
        X509_get0_signature(&signature, nullptr, cert.get());
        return {signature->data, signature->data + signature->length};
    }
    Bytes sign(const Bytes& data)
    {
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        check(ctx && EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1);
        size_t size = 0;
        check(EVP_DigestSign(ctx.get(), nullptr, &size, data.data(), data.size()) == 1);
        Bytes signature(size);
        check(EVP_DigestSign(ctx.get(), signature.data(), &size, data.data(), data.size()) == 1);
        signature.resize(size);
        return signature;
    }
    NativePairing::Response exchange(bool tls, const std::string& arguments, int timeout, const char* label) override
    {
        ++stage;
        check(stage <= 5 && tls == (stage == 5) && timeout == (stage == 1 ? 0 : 5000));
        check(std::string(label).find("pair stage " + std::to_string(stage)) == 0);
        if (stage == throwStage) throw std::runtime_error("owner transport error");
        std::map<std::string, std::string> fields;
        size_t begin = 0;
        while (begin < arguments.size()) {
            auto end = arguments.find('&', begin);
            if (end == std::string::npos) end = arguments.size();
            auto equals = arguments.find('=', begin);
            check(equals < end);
            fields[arguments.substr(begin, equals - begin)] = arguments.substr(equals + 1, end - equals - 1);
            begin = end + 1;
        }
        check(fields["devicename"] == "roth" && fields["updateState"] == "1");
        NativePairing::Response response;
        response.paired = stage != failStage;
        if (!response.paired) return response;
        if (stage == 1) {
            check(fields["phrase"] == "getservercert");
            const auto client = unhex(fields["clientcert"]);
            check(std::string(client.begin(), client.end()) == pem);
            auto input = unhex(fields["salt"]);
            check(input.size() == 16);
            const std::string pin = badPin ? "9999" : "1234";
            input.insert(input.end(), pin.begin(), pin.end());
            aesKey = digest(input, generation);
            aesKey.resize(16);
            response.certificatePem = busy ? "" : malformedCert ? "invalid certificate" : pem;
            response.pairName = "測試主機";
        }
        else if (stage == 2) {
            check(pinned);
            challenge = crypt(unhex(fields["clientchallenge"]), aesKey, false);
            check(challenge.size() == 16);
            auto input = challenge;
            append(input, certSignature()); append(input, serverSecret);
            auto output = digest(input, generation);
            if (!shortChallenge) append(output, serverChallenge);
            output.resize(shortChallenge ? 32 : 48, 0);
            response.challengeResponse = crypt(output, aesKey, true);
            if (nonBlockChallenge) response.challengeResponse.pop_back();
        }
        else if (stage == 3) {
            responseHash = crypt(unhex(fields["serverchallengeresp"]), aesKey, false);
            check(responseHash.size() == 32);
            response.pairingSecret = serverSecret;
            auto signature = sign(serverSecret);
            if (badSignature) signature[0] ^= 1;
            if (!shortSecret) append(response.pairingSecret, signature);
        }
        else if (stage == 4) {
            auto bytes = unhex(fields["clientpairingsecret"]);
            check(bytes.size() > 16);
            Bytes secret(bytes.begin(), bytes.begin() + 16);
            auto input = serverChallenge;
            append(input, certSignature()); append(input, secret);
            const auto expected = digest(input, generation);
            check(std::equal(expected.begin(), expected.end(), responseHash.begin()));
            std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
            check(ctx && EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1);
            check(EVP_DigestVerify(ctx.get(), bytes.data() + 16, bytes.size() - 16, secret.data(), secret.size()) == 1);
        }
        else check(pinned && fields["phrase"] == "pairchallenge");
        return response;
    }
    void unpair() override { ++unpairs; }
    bool pinCertificate(const std::string& certificate) override { check(certificate == pem); pinned = !rejectPin; return pinned; }
    void savePairName(const std::string& name) override { check(!pinned); savedName = name; }
};
int main(int argc, char** argv)
{
    try {
        if (argc == 5 && std::string(argv[1]) == "--peer") {
            const auto pem = file(std::string(argv[2]) + "/server.pem");
            Peer peer(std::stoi(argv[3]), pem, file(std::string(argv[2]) + "/server-key.pem"));
            peer.pinned = true; // Server role; the real client must pin its TLS request.
            const std::string mode = argv[4];
            peer.busy = mode == "busy"; peer.badSignature = mode == "bad-signature";
            peer.shortChallenge = mode == "short-challenge";
            peer.malformedCert = mode == "bad-certificate";
            peer.shortSecret = mode == "short-secret";
            if (mode == "final-reject") peer.failStage = 5;
            std::string line;
            while (std::getline(std::cin, line)) {
                check(line.size() > 2);
                const auto response = peer.exchange(line[0] == '1', line.substr(2), peer.stage == 0 ? 0 : 5000,
                    ("pair stage " + std::to_string(peer.stage + 1)).c_str());
                auto encode = [](const Bytes& bytes) { return bytes.empty() ? std::string("-") : hex(bytes); };
                std::cout << response.paired << ' ' << encode(Bytes(response.certificatePem.begin(), response.certificatePem.end())) << ' '
                    << encode(Bytes(response.pairName.begin(), response.pairName.end())) << ' '
                    << encode(response.challengeResponse) << ' ' << encode(response.pairingSecret) << std::endl;
            }
            return 0;
        }
        check(argc == 2);
        const auto pem = file(std::string(argv[1]) + "/server.pem");
        const auto key = file(std::string(argv[1]) + "/server-key.pem");
        Bytes salt;
        for (unsigned char i = 0; i < 16; ++i) salt.push_back(i);
        check(hex(NativePairing::deriveKey(salt, "0214", 6)) == "0083626da4cdf3bf8f34fc8204d09b67");
        check(hex(NativePairing::deriveKey(salt, "0605", 7)) == "0062d88815ca075e125da2dc72afcc73");
        std::cout << "Binary SHA-1/SHA-256 key derivation, including leading zero: passed\n";
        for (int generation : {6, 7}) {
            for (int mode = 0; mode < 10; ++mode) {
                Peer peer(generation, pem, key);
                peer.busy = mode == 1; peer.malformedCert = mode == 2;
                peer.badSignature = mode == 3; peer.badPin = mode == 4;
                peer.shortChallenge = mode == 5; peer.nonBlockChallenge = mode == 6;
                peer.shortSecret = mode == 7; peer.rejectPin = mode == 8;
                if (mode == 9) peer.failStage = 1;
                std::string output = "unchanged";
                const auto result = NativePairing::pair(peer, generation, "1234", pem, key, output);
                const auto expected = mode == 0 ? NativePairing::State::Paired : mode == 1 ? NativePairing::State::AlreadyInProgress :
                                      mode == 4 ? NativePairing::State::PinWrong : NativePairing::State::Failed;
                check(result == expected && peer.unpairs == (mode == 0 || mode == 9 ? 0 : 1));
                check(output == (mode == 0 ? pem : "unchanged"));
                if (mode == 0) check(peer.stage == 5 && peer.savedName == "測試主機");
            }
            for (int stage = 2; stage <= 5; ++stage) {
                Peer peer(generation, pem, key);
                peer.failStage = stage;
                std::string output = "unchanged";
                check(NativePairing::pair(peer, generation, "1234", pem, key, output) == NativePairing::State::Failed);
                check(peer.stage == stage && peer.unpairs == 1 && output == "unchanged");
            }
            for (int stage = 1; stage <= 5; ++stage) {
                Peer peer(generation, pem, key);
                peer.throwStage = stage;
                std::string output = "unchanged";
                bool thrown = false;
                try { NativePairing::pair(peer, generation, "1234", pem, key, output); }
                catch (const std::runtime_error& error) { thrown = std::string(error.what()) == "owner transport error"; }
                check(thrown && peer.stage == stage && peer.unpairs == 0 && output == "unchanged");
            }
            std::cout << "Generation " << generation << " five-stage pairing, failure/cleanup and owner errors: passed\n";
        }
        for (bool badCert : {false, true}) {
            Peer peer(7, pem, key);
            std::string output = "unchanged";
            bool thrown = false;
            try { NativePairing::pair(peer, 7, "1234", badCert ? "invalid" : pem, badCert ? key : "invalid", output); }
            catch (const std::runtime_error&) { thrown = true; }
            check(thrown && peer.stage == 0 && output == "unchanged");
        }
        std::cout << "Invalid local credentials fail before host requests: passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
