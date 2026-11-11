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

Image decode(std::string_view png, size_t maxPixels);
}
