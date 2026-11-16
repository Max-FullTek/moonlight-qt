#include "nativeclipboardipc.h"
#include "clipboardblobpolicy.h"
#include "clipboardpipelines.h"

#include <windows.h>
#include <wincrypt.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 && NLOHMANN_JSON_VERSION_MINOR == 12 &&
              NLOHMANN_JSON_VERSION_PATCH == 0, "Use the verified nlohmann/json release");

namespace NativeClipboardIpc {
namespace {
using Json = nlohmann::json;

constexpr std::pair<Type, std::string_view> MessageTypes[] = {
    {Type::Configure, "configure"}, {Type::HostFrame, "hostFrame"}, {Type::LocalFrame, "localFrame"},
    {Type::Ready, "ready"}, {Type::Error, "error"}, {Type::Stop, "stop"},
    {Type::Ping, "ping"}, {Type::Pong, "pong"},
};

Json readObject(std::string_view payload)
{
    if (payload.find('\0') != std::string_view::npos) return nullptr;
    struct NestingLimit {};
    try {
        auto parsed = Json::parse(payload.begin(), payload.end(),
            [](int depth, Json::parse_event_t, Json&) {
                if (depth > 128) throw NestingLimit{};
                return true;
            }, false);
        if (parsed.is_object()) return parsed;
    } catch (const NestingLimit&) {
        // A complete IPC message uses at most two levels.
    }
    return nullptr;
}

std::string stringField(const Json& object, const char* key)
{
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

std::optional<uint32_t> unsignedField(const Json& object, const char* key, uint32_t max)
{
    const auto value = object.find(key);
    if (value == object.end() || !value->is_number()) return std::nullopt;
    const auto number = value->get<double>();
    if (!std::isfinite(number) || number < 0 || number > max || std::floor(number) != number) return std::nullopt;
    return static_cast<uint32_t>(number);
}

std::string decodeBase64(const Json& object, const char* key)
{
    const auto encoded = stringField(object, key);
    DWORD size = 0;
    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT;
    if (encoded.empty() || !CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()),
                                                 flags, nullptr, &size, nullptr, nullptr)) return {};
    std::string binary(size, '\0');
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), flags,
                              reinterpret_cast<BYTE*>(binary.data()), &size, nullptr, nullptr)) return {};
    binary.resize(size);
    return binary;
}

std::string encodeBase64(std::string_view binary)
{
    DWORD size = 0;
    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    const auto* bytes = reinterpret_cast<const BYTE*>(binary.data());
    const auto length = static_cast<DWORD>(binary.size());
    if (binary.empty() || !CryptBinaryToStringA(bytes, length, flags, nullptr, &size)) return {};
    std::string encoded(size, '\0');
    if (!CryptBinaryToStringA(bytes, length, flags, encoded.data(), &size)) return {};
    encoded.resize(size);
    return encoded;
}
}

bool decode(std::string_view line, Message& message, std::string& error)
{
    message = {};
    error.clear();
    if (line.size() > ClipboardPipe::MaxLineBytes) {
        error = "message line too large";
        return false;
    }
    const auto object = readObject(line);
    if (object.is_null()) {
        error = "invalid JSON message";
        return false;
    }
    if (unsignedField(object, "version", 1) != 1) {
        error = "unsupported protocol version";
        return false;
    }
    const auto sequence = unsignedField(object, "sequence", (std::numeric_limits<uint32_t>::max)());
    if (!sequence) {
        error = "invalid sequence number";
        return false;
    }
    message.sequence = *sequence;
    const auto name = stringField(object, "type");
    for (const auto& entry : MessageTypes) {
        if (name == entry.second) {
            message.type = entry.first;
            break;
        }
    }
    switch (message.type) {
    case Type::Configure:
        if (const auto host = object.find("host"); host != object.end() && host->is_object()) {
            message.host.address = stringField(*host, "address");
            message.host.httpsPort = static_cast<uint16_t>(unsignedField(*host, "httpsPort", 65535).value_or(0));
            message.host.serverCertPem = decodeBase64(*host, "serverCertPem");
            message.host.clientCertPem = decodeBase64(*host, "clientCertPem");
            message.host.clientKeyPem = decodeBase64(*host, "clientKeyPem");
        }
        return true;
    case Type::HostFrame:
    case Type::LocalFrame:
        message.frame = decodeBase64(object, "payload");
        if (!message.frame.empty()) return true;
        error = "missing or invalid frame payload";
        return false;
    case Type::Error:
        message.code = stringField(object, "code");
        message.text = stringField(object, "text");
        return true;
    case Type::Unknown:
        error = "unknown message type";
        return false;
    default:
        return true;
    }
}

std::string encode(const Message& message)
{
    const auto type = std::find_if(std::begin(MessageTypes), std::end(MessageTypes),
        [&message](const auto& entry) { return entry.first == message.type; });
    Json object{{"version", 1}, {"sequence", message.sequence},
                {"type", type == std::end(MessageTypes) ? "unknown" : type->second}};
    switch (message.type) {
    case Type::Configure:
        object["host"] = {{"address", message.host.address}, {"httpsPort", message.host.httpsPort},
                          {"serverCertPem", encodeBase64(message.host.serverCertPem)},
                          {"clientCertPem", encodeBase64(message.host.clientCertPem)},
                          {"clientKeyPem", encodeBase64(message.host.clientKeyPem)}};
        break;
    case Type::HostFrame:
    case Type::LocalFrame:
        object["payload"] = encodeBase64(message.frame);
        break;
    case Type::Error:
        object["code"] = message.code;
        object["text"] = message.text;
        break;
    default:
        break;
    }
    return object.dump();
}

bool validBlobId(std::string_view id)
{
    return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::optional<BlobReference> decodeReference(std::string_view payload)
{
    if (payload.size() > ClipboardPipe::MaxLineBytes) return std::nullopt;
    const auto object = readObject(payload);
    auto id = stringField(object, "id");
    const auto size = unsignedField(object, "size", ClipboardBlob::MaxBytes);
    if (!size || !validBlobId(id)) return std::nullopt;
    return BlobReference{std::move(id), stringField(object, "mime"), *size};
}

std::optional<std::string> decodeUploadId(std::string_view payload)
{
    if (payload.size() > ClipboardBlob::MaxUploadReplyBytes) return std::nullopt;
    auto id = stringField(readObject(payload), "id");
    if (!validBlobId(id)) return std::nullopt;
    return id;
}

std::string encodeReference(const BlobReference& reference)
{
    return Json{{"id", reference.id}, {"mime", reference.mime}, {"size", reference.size}}.dump();
}
}
