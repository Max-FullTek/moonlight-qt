#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace WindowsClipboardPng {
struct Image {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<unsigned char> rgba;
};

// WIC can decode PNGs with a bad IDAT CRC. Callers requiring the existing
// clipboard integrity checks must validate the PNG before using this decoder.
Image decode(std::string_view png, size_t maxPixels);
}
