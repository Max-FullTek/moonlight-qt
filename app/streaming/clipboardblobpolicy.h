#pragma once

#include <cstdint>

namespace ClipboardBlob {

inline constexpr std::int64_t MaxBytes = 64LL * 1024 * 1024;
inline constexpr int MaxPendingTransfers = 4;
inline constexpr int TimeoutMs = 30000;
inline constexpr int MaxUploadReplyBytes = 16 * 1024;

constexpr std::int64_t downloadLimit(std::int64_t advertisedSize)
{
    return advertisedSize > 0 ? advertisedSize : MaxBytes;
}

}
