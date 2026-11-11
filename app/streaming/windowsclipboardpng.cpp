#include "windowsclipboardpng.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <limits>

using Microsoft::WRL::ComPtr;

WindowsClipboardPng::Image WindowsClipboardPng::decode(std::string_view png, size_t maxPixels)
{
    constexpr auto maxBuffer = (std::numeric_limits<UINT>::max)();
    if (png.empty() || png.size() > maxBuffer) return {};

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
