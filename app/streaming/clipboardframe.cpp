#include "clipboardframe.h"

namespace ClipboardFrame {
namespace {

void storeLe32(char* out, uint32_t value)
{
    auto* bytes = reinterpret_cast<unsigned char*>(out);
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = static_cast<unsigned char>(value >> (8 * i));
    }
}

uint32_t readLe32(const char* in)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= uint32_t(static_cast<unsigned char>(in[i])) << (8 * i);
    }
    return value;
}

}

std::optional<Header> encodeHeader(uint8_t kind, uint32_t token, size_t payloadSize)
{
    if (payloadSize > MaxInlinePayload) return std::nullopt;

    Header header{Version, static_cast<char>(kind)};
    storeLe32(header.data() + 2, token);
    storeLe32(header.data() + 6, static_cast<uint32_t>(payloadSize));
    return header;
}

std::optional<View> decode(std::string_view frame)
{
    if (frame.size() < HeaderSize || frame[0] != Version) return std::nullopt;

    const auto payload = frame.substr(HeaderSize);
    if (payload.size() > MaxPayload || payload.size() != readLe32(frame.data() + 6)) {
        return std::nullopt;
    }
    return View{static_cast<uint8_t>(frame[1]), readLe32(frame.data() + 2), payload};
}

}
