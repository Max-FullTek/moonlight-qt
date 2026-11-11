#include "windowsclipboardpng.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {
constexpr auto PngCrcTable = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t index = 0; index < table.size(); ++index) {
        uint32_t remainder = index;
        for (int bit = 0; bit < 8; ++bit) {
            remainder = (remainder >> 1) ^ ((remainder & 1) ? 0xedb88320u : 0u);
        }
        table[index] = remainder;
    }
    return table;
}();

uint32_t readBigEndian(const char* bytes)
{
    uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value = (value << 8) | static_cast<unsigned char>(bytes[index]);
    }
    return value;
}

bool readChunks(std::string_view png, WindowsClipboardPng::Metadata metadata,
                WindowsClipboardPng::Image& image)
{
    constexpr std::string_view signature("\x89PNG\r\n\x1a\n", 8);
    if (png.substr(0, signature.size()) != signature) return false;
    png.remove_prefix(signature.size());
    bool seenImageData = false;

    // The CRC covers the type and data, excluding the length and CRC fields.
    while (png.size() >= 12) {
        const size_t dataSize = readBigEndian(png.data());
        if (dataSize > png.size() - 12 || dataSize > 0x7fffffffu) return false;
        const auto type = png.substr(4, 4);
        uint32_t crc = 0xffffffffu;
        for (const unsigned char byte : png.substr(4, 4 + dataSize)) {
            crc = PngCrcTable[(crc ^ byte) & 0xff] ^ (crc >> 8);
        }
        if ((crc ^ 0xffffffffu) != readBigEndian(png.data() + 8 + dataSize)) return false;
        if (type == "IDAT") seenImageData = true;
        if (type == "IEND") return dataSize == 0;
        if (metadata == WindowsClipboardPng::Metadata::PreserveForPaste) {
            if (type == "pHYs") {
                if (seenImageData || image.dotsPerMeterX || dataSize != 9 || png[16] != 1) return false;
                const auto x = readBigEndian(png.data() + 8);
                const auto y = readBigEndian(png.data() + 12);
                constexpr auto maxResolution = static_cast<uint32_t>((std::numeric_limits<int>::max)());
                if (!x || !y || x > maxResolution || y > maxResolution) return false;
                image.dotsPerMeterX = static_cast<int>(x);
                image.dotsPerMeterY = static_cast<int>(y);
            } else if (type == "sRGB") {
                if (seenImageData || image.srgb || dataSize != 1 ||
                    static_cast<unsigned char>(png[8]) > 3) return false;
                image.srgb = true;
            } else if (type != "IHDR" && type != "PLTE" && type != "tRNS" && type != "IDAT") {
                return false;
            }
        }
        png.remove_prefix(12 + dataSize);
    }
    return false;
}

bool supportedPixelFormat(unsigned depth, unsigned color)
{
    // Keep 16-bit samples on the path that retains their original precision.
    if (depth == 1 || depth == 2 || depth == 4) return color == 0 || color == 3;
    return depth == 8 && (color == 0 || color == 2 || color == 3 || color == 4 || color == 6);
}
}

WindowsClipboardPng::Image WindowsClipboardPng::decode(std::string_view png, size_t maxPixels,
                                                       Metadata metadata)
{
    constexpr auto maxBuffer = (std::numeric_limits<UINT>::max)();
    Image image;
    if (png.size() < 33 || png.size() > maxBuffer ||
        png.substr(12, 4) != "IHDR" ||
        !supportedPixelFormat(static_cast<unsigned char>(png[24]), static_cast<unsigned char>(png[25])) ||
        !readChunks(png, metadata, image)) return {};

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return {};
    const struct Apartment {
        HRESULT result;
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment{initialized};

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(
            reinterpret_cast<BYTE*>(const_cast<char*>(png.data())), static_cast<DWORD>(png.size()))) ||
        FAILED(CoCreateInstance(CLSID_WICPngDecoder, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(decoder.GetAddressOf()))) ||
        FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return {};

    if (FAILED(frame->GetSize(&image.width, &image.height)) ||
        !image.width || !image.height) return {};
    const auto pixels = static_cast<size_t>(image.width) * image.height;
    if (pixels > maxPixels || pixels > maxBuffer / 4) return {};

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};

    image.rgba.resize(pixels * 4);
    if (FAILED(converter->CopyPixels(nullptr, image.width * 4,
                                     static_cast<UINT>(image.rgba.size()), image.rgba.data()))) return {};
    return image;
}
