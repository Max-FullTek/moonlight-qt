#include "windowsclipboardpng.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <limits>
#include <utility>

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
        if (metadata != WindowsClipboardPng::Metadata::PixelsOnly) {
            const bool strict = metadata == WindowsClipboardPng::Metadata::PreserveForPaste;
            if (type == "pHYs") {
                if (seenImageData || image.dotsPerMeterX || dataSize != 9 || png[16] != 1) {
                    if (strict) return false;
                    png.remove_prefix(12 + dataSize);
                    continue;
                }
                const auto x = readBigEndian(png.data() + 8);
                const auto y = readBigEndian(png.data() + 12);
                constexpr auto maxResolution = static_cast<uint32_t>((std::numeric_limits<int>::max)());
                if (x && y && x <= maxResolution && y <= maxResolution) {
                    image.dotsPerMeterX = static_cast<int>(x);
                    image.dotsPerMeterY = static_cast<int>(y);
                } else if (strict) return false;
            } else if (type == "sRGB") {
                if (seenImageData || image.srgb || dataSize != 1 ||
                    static_cast<unsigned char>(png[8]) > 3) return false;
                image.srgb = true;
            } else if (strict && type != "IHDR" && type != "PLTE" && type != "tRNS" && type != "IDAT") {
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

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool ready() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};

constexpr auto MaxWicBuffer = (std::numeric_limits<UINT>::max)();

struct BitmapDecoder {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;

    bool open(std::string_view bytes, const CLSID& codec)
    {
        return !bytes.empty() && bytes.size() <= MaxWicBuffer &&
            SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(factory.GetAddressOf()))) &&
            SUCCEEDED(factory->CreateStream(stream.GetAddressOf())) &&
            SUCCEEDED(stream->InitializeFromMemory(
                reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())), static_cast<DWORD>(bytes.size()))) &&
            SUCCEEDED(CoCreateInstance(codec, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(decoder.GetAddressOf()))) &&
            SUCCEEDED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) &&
            SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf()));
    }

    bool readPixels(WindowsClipboardPng::Image& image, size_t maxPixels, const WICPixelFormatGUID& format)
    {
        if (FAILED(frame->GetSize(&image.width, &image.height)) || !image.width || !image.height) return false;
        const auto pixels = static_cast<size_t>(image.width) * image.height;
        if (pixels > maxPixels || pixels > MaxWicBuffer / 4) return false;

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(frame.Get(), format,
                                         WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;

        image.rgba.resize(pixels * 4);
        return SUCCEEDED(converter->CopyPixels(nullptr, image.width * 4,
            static_cast<UINT>(image.rgba.size()), image.rgba.data()));
    }
};
}

WindowsClipboardPng::Image WindowsClipboardPng::decode(std::string_view png, size_t maxPixels,
                                                       Metadata metadata)
{
    Image image;
    if (png.size() < 33 || png.size() > MaxWicBuffer ||
        png.substr(12, 4) != "IHDR" ||
        (!(metadata == Metadata::NativeClipboard && static_cast<unsigned char>(png[24]) == 16) &&
         !supportedPixelFormat(static_cast<unsigned char>(png[24]), static_cast<unsigned char>(png[25]))) ||
        !readChunks(png, metadata, image)) return {};

    const Apartment apartment;
    BitmapDecoder decoder;
    if (!apartment.ready() || !decoder.open(png, CLSID_WICPngDecoder) ||
        !decoder.readPixels(image, maxPixels, GUID_WICPixelFormat32bppRGBA)) return {};
    return image;
}

WindowsClipboardPng::BitmapConversion WindowsClipboardPng::convertBitmap(
    std::string_view bmp, size_t maxPixels, size_t maxPngBytes)
{
    const Apartment apartment;
    BitmapDecoder decoder;
    BitmapConversion result;
    if (!apartment.ready() || !decoder.open(bmp, CLSID_WICBmpDecoder)) return {};
    UINT colorContexts = 0;
    if (SUCCEEDED(decoder.frame->GetColorContexts(0, nullptr, &colorContexts)) && colorContexts != 0) {
        // Leave profile-bearing bitmaps to the current compatibility path.
        return {};
    }
    if (!decoder.readPixels(result.image, maxPixels, GUID_WICPixelFormat32bppBGRA)) return {};

    ComPtr<IStream> output;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, output.GetAddressOf())) ||
        FAILED(decoder.factory->CreateEncoder(GUID_ContainerFormatPng, &GUID_VendorMicrosoft, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(output.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) ||
        FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(result.image.width, result.image.height))) return {};

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) return {};
    double dpiX = 0, dpiY = 0;
    if (SUCCEEDED(decoder.frame->GetResolution(&dpiX, &dpiY)) && dpiX > 0 && dpiY > 0 &&
        FAILED(frame->SetResolution(dpiX, dpiY))) return {};
    auto& pixels = result.image.rgba;
    if (FAILED(frame->WritePixels(result.image.height, result.image.width * 4,
                                  static_cast<UINT>(pixels.size()), pixels.data())) ||
        FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) return {};

    STATSTG stats{};
    if (FAILED(output->Stat(&stats, STATFLAG_NONAME)) || !stats.cbSize.QuadPart ||
        stats.cbSize.QuadPart > maxPngBytes || stats.cbSize.QuadPart > MaxWicBuffer ||
        FAILED(output->Seek({}, STREAM_SEEK_SET, nullptr))) return {};
    result.png.resize(static_cast<size_t>(stats.cbSize.QuadPart));
    ULONG read = 0;
    if (FAILED(output->Read(result.png.data(), static_cast<ULONG>(result.png.size()), &read)) ||
        read != result.png.size()) return {};
    // The encoder accepts BGRA. Keep the returned pixels in the same channel
    // order as the PNG decoder for clipboard echo comparison.
    for (size_t index = 0; index < pixels.size(); index += 4) {
        std::swap(pixels[index], pixels[index + 2]);
    }
    return result;
}
