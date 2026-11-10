#pragma once
#include <string>
#include <vector>

namespace NativePairing {
using Bytes = std::vector<unsigned char>;
enum class State { Paired, PinWrong, Failed, AlreadyInProgress };
struct Response {
    bool paired = false;
    std::string certificatePem;
    std::string pairName;
    Bytes challengeResponse;
    Bytes pairingSecret;
};
// Host-control/serialization and storage remain with the existing owner.
// Exceptions from exchange/unpair propagate, preserving owner error policy.
class Host {
public:
    virtual ~Host() = default;
    virtual Response exchange(bool tls, const std::string& arguments, int timeoutMs, const char* stage) = 0;
    virtual void unpair() = 0;
    virtual bool pinCertificate(const std::string& pem) = 0;
    virtual void savePairName(const std::string& name) = 0;
};
// Protocol key derivation; the 16-byte result is binary, never a C string.
Bytes deriveKey(const Bytes& salt, const std::string& pin, int serverMajorVersion);
// Entire five-stage protocol and crypto; no Qt, networking, files, threads or
// persistence. certificatePem is assigned only after the final paired reply.
State pair(Host& host, int serverMajorVersion, const std::string& pin,
           const std::string& clientCertificatePem, const std::string& clientPrivateKeyPem,
           std::string& certificatePem);
}
