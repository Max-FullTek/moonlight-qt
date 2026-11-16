#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace NativeClipboardIpc {
enum class Type { Unknown, Configure, HostFrame, LocalFrame, Ready, Error, Stop, Ping, Pong };

struct Host {
    std::string address;
    uint16_t httpsPort = 0;
    std::string serverCertPem;
    std::string clientCertPem;
    std::string clientKeyPem;
};

struct Message {
    Type type = Type::Unknown;
    uint32_t sequence = 0;
    Host host;
    std::string frame;
    std::string code;
    std::string text;
};

bool decode(std::string_view line, Message& message, std::string& error);
std::string encode(const Message& message);

struct BlobReference {
    std::string id;
    std::string mime;
    int64_t size = 0;
};

bool validBlobId(std::string_view id);
std::optional<BlobReference> decodeReference(std::string_view payload);
std::optional<std::string> decodeUploadId(std::string_view payload);
std::string encodeReference(const BlobReference& reference);
}
