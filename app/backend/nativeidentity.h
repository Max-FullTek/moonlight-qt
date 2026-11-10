#pragma once
#include <string>

namespace NativeIdentity {
struct Credentials {
    std::string certificatePem;
    std::string privateKeyPem;
};
// Existing GameStream identity parameters and PEM format. No storage, Qt,
// transport, certificate trust decisions, or regeneration policy lives here.
// Failure throws without publishing a partial identity.
Credentials generateCredentials();
std::string generateUniqueId();
// Preserve PKCS8 on Windows and the existing PKCS1 adapter on Darwin. Invalid
// or password-protected input returns empty; this function never prompts.
std::string normalizePrivateKey(const std::string& pem, bool traditional);
}
