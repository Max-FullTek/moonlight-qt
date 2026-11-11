#include "streaming/windowsclipboardpng.h"

#include <cstdio>
#include <string>
#include <windows.h>

namespace {
// Two pixels with partial and zero alpha, encoded as RGBA and indexed PNG.
constexpr char RgbaPng[] =
    "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52"
    "\x00\x00\x00\x02\x00\x00\x00\x01\x08\x06\x00\x00\x00\xf4\x22\x7f"
    "\x8a\x00\x00\x00\x11\x49\x44\x41\x54\x78\x9c\x63\x10\xd7\x75\x6e"
    "\x38\x99\x6e\xce\x00\x00\x0a\xd5\x02\x6f\xfe\x54\x7a\x8c\x00\x00"
    "\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
constexpr char IndexedPng[] =
    "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52"
    "\x00\x00\x00\x02\x00\x00\x00\x01\x08\x03\x00\x00\x00\xc3\xfc\x8f"
    "\xb8\x00\x00\x00\x06\x50\x4c\x54\x45\x17\x2d\x43\xc9\x67\x37\x07"
    "\x3b\x62\x2a\x00\x00\x00\x02\x74\x52\x4e\x53\x80\x00\x4d\x10\x55"
    "\x73\x00\x00\x00\x0b\x49\x44\x41\x54\x78\x9c\x63\x60\x60\x04\x00"
    "\x00\x04\x00\x02\xbf\x7a\x3f\x4a\x00\x00\x00\x00\x49\x45\x4e\x44"
    "\xae\x42\x60\x82";

bool checkImage(std::string_view input)
{
    const std::vector<unsigned char> expected{23, 45, 67, 128, 201, 103, 55, 0};
    const auto image = WindowsClipboardPng::decode(input, 2);
    return image.width == 2 && image.height == 1 && image.rgba == expected &&
           WindowsClipboardPng::decode(input, 1).rgba.empty();
}
}

int main()
{
    constexpr std::string_view rgba{RgbaPng, sizeof(RgbaPng) - 1};
    constexpr std::string_view indexed{IndexedPng, sizeof(IndexedPng) - 1};
    std::string badIdatChecksum(rgba);
    badIdatChecksum[rgba.size() - 13] ^= 1;
    std::string oversizedChunk(rgba);
    oversizedChunk[8] = '\x7f';
    constexpr char resolutionChunk[] =
        "\x00\x00\x00\x09\x70\x48\x59\x73\x00\x00\x2e\x23\x00\x00\x2e\x23\x01\x78\xa5\x3f\x76";
    constexpr char srgbChunk[] = "\x00\x00\x00\x01\x73\x52\x47\x42\x00\xae\xce\x1c\xe9";
    constexpr char textChunk[] =
        "\x00\x00\x00\x0d\x74\x45\x58\x74\x54\x69\x74\x6c\x65\x00\x46\x69\x78\x74\x75\x72\x65\x80\x8f\x8b\x10";
    std::string withMetadata(rgba);
    withMetadata.insert(33, resolutionChunk, sizeof(resolutionChunk) - 1);
    withMetadata.insert(33, srgbChunk, sizeof(srgbChunk) - 1);
    std::string withText(withMetadata);
    withText.insert(33, textChunk, sizeof(textChunk) - 1);
    using WindowsClipboardPng::Metadata;
    const auto metadataImage = WindowsClipboardPng::decode(withMetadata, 2, Metadata::PreserveForPaste);
    if (!checkImage(rgba) || !checkImage(indexed) ||
        WindowsClipboardPng::decode(rgba, 2, Metadata::PreserveForPaste).rgba.empty() ||
        WindowsClipboardPng::decode(indexed, 2, Metadata::PreserveForPaste).rgba.empty() ||
        !checkImage(withMetadata) || metadataImage.rgba != WindowsClipboardPng::decode(rgba, 2).rgba ||
        metadataImage.dotsPerMeterX != 11811 || metadataImage.dotsPerMeterY != 11811 || !metadataImage.srgb ||
        !checkImage(withText) ||
        !WindowsClipboardPng::decode(withText, 2, Metadata::PreserveForPaste).rgba.empty() ||
        !WindowsClipboardPng::decode(badIdatChecksum, 2).rgba.empty() ||
        !WindowsClipboardPng::decode(oversizedChunk, 2).rgba.empty() ||
        !WindowsClipboardPng::decode(rgba.substr(0, rgba.size() - 12), 2).rgba.empty() ||
        !WindowsClipboardPng::decode(rgba.substr(0, 30), 2).rgba.empty() ||
        !WindowsClipboardPng::decode("not a PNG", 2).rgba.empty()) {
        std::fputs("WIC PNG decode or input limit check failed\n", stderr);
        return 1;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    const bool decodedInSta = checkImage(rgba);
    CoUninitialize();
    return decodedInSta ? 0 : 1;
}
