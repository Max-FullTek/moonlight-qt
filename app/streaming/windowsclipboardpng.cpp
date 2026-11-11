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

bool hasValidChunkChecksums(std::string_view png, WindowsClipboardPng::Metadata metadata)
{
    constexpr std::string_view signature("\x89PNG\r\n\x1a\n", 8);
    if (png.substr(0, signature.size()) != signature) return false;
    png.remove_prefix(signature.size());

    // The CRC covers the type and data, excluding the length and CRC fields.
    while (png.size() >= 12) {
        const size_t dataSize = readBigEndian(png.data());
        if (dataSize > png.size() - 12 || dataSize > 0x7fffffffu) return false;
        const auto type = png.substr(4, 4);
        // The RGBA buffer does not carry color profiles, resolution, or other
        // metadata. Let the caller use its metadata-aware decoder for paste.
        if (metadata == WindowsClipboardPng::Metadata::PreserveForPaste &&
            type != "IHDR" && type != "PLTE" && type != "tRNS" &&
            type != "IDAT" && type != "IEND") return false;
        uint32_t crc = 0xffffffffu;
        for (const unsigned char byte : png.substr(4, 4 + dataSize)) {
            crc = PngCrcTable[(crc ^ byte) & 0xff] ^ (crc >> 8);
        }
        if ((crc ^ 0xffffffffu) != readBigEndian(png.data() + 8 + dataSize)) return false;
        if (type == "IEND") return dataSize == 0;
        png.remove_prefix(12 + dataSize);
    }
    return false;
}
}

WindowsClipboardPng::Image WindowsClipboardPng::decode(std::string_view png, size_t maxPixels,
                                                       Metadata metadata)
{
    constexpr auto maxBuffer = (std::numeric_limits<UINT>::max)();
    // Keep pixel hashing consistent with the clipboard's existing decoder.
    // This path is verified for 8-bit RGBA and indexed PNGs.
    if (png.size() < 33 || png.size() > maxBuffer ||
        png.substr(12, 4) != "IHDR" || png[24] != 8 ||
        (png[25] != 3 && png[25] != 6) || !hasValidChunkChecksums(png, metadata)) return {};

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

    Image image;
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
