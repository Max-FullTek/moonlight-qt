#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ClipboardFrame {

inline constexpr uint8_t Version = 1;
inline constexpr uint8_t Text = 1;
inline constexpr uint8_t Png = 2;
inline constexpr uint8_t Reference = 3;
inline constexpr int HeaderSize = 10;
inline constexpr int MaxPayload = 65535 - HeaderSize;
// Outbound encryption adds the sequence, GCM tag and inner control header.
inline constexpr int MaxInlinePayload = MaxPayload - 24;
inline constexpr int InlineThreshold = 60000;

using Header = std::array<char, HeaderSize>;

struct View {
    uint8_t kind;
    uint32_t token;
    std::string_view payload;
};

std::optional<Header> encodeHeader(uint8_t kind, uint32_t token, size_t payloadSize);
// The returned payload borrows the input buffer.
std::optional<View> decode(std::string_view frame);

}
