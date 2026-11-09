#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "nativenotification.h"
#include "SDL_compat.h"
#include <SDL_syswm.h>
#include <d3d11.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <climits>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
// Internal HRESULT propagation keeps every BeginDraw paired with EndDraw and
// avoids logging/UI policy in the rendering module.
void checked(HRESULT value) { if (FAILED(value)) throw value; }
}

struct NativeNotification::Impl {
    SDL_Window* window;
    DWORD thread = GetCurrentThreadId();
    ComPtr<ID3D11Device> gpu;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> device;
    ComPtr<ID2D1DeviceContext> draw;
    ComPtr<IDWriteFactory> textFactory;
    std::wstring message;
    Uint64 deadline = 0;
    bool showing = false;

    explicit Impl(SDL_Window* owner) : window(owner) {}
    ~Impl()
    {
        // Remove the layer while the owner's HWND is still valid. Destruction
        // cannot report device-loss errors; no replacement window is created.
        if (target && GetCurrentThreadId() == thread) {
            target->SetRoot(nullptr);
            composition->Commit();
        }
    }

    void initialize()
    {
        if (composition) return;
        if (!window) throw HRESULT(E_INVALIDARG);
        SDL_SysWMinfo info = {};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS)
            throw HRESULT(E_HANDLE);

        // Build in locals so a failed initialization can be retried without a
        // half-initialized target surviving on the HWND.
        ComPtr<ID3D11Device> newGpu;
        checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &newGpu, nullptr, nullptr));
        ComPtr<IDXGIDevice> dxgi;
        checked(newGpu.As(&dxgi));
        ComPtr<IDCompositionDevice> newComposition;
        checked(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&newComposition)));
        ComPtr<IDCompositionTarget> newTarget;
        checked(newComposition->CreateTargetForHwnd(info.info.win.window, TRUE, &newTarget));
        ComPtr<IDCompositionVisual> newVisual;
        checked(newComposition->CreateVisual(&newVisual));
        checked(newTarget->SetRoot(newVisual.Get()));
        ComPtr<ID2D1Factory1> newFactory;
        const D2D1_FACTORY_OPTIONS options = {};
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                 &options, reinterpret_cast<void**>(newFactory.GetAddressOf())));
        ComPtr<ID2D1Device> newDevice;
        checked(newFactory->CreateDevice(dxgi.Get(), &newDevice));
        ComPtr<ID2D1DeviceContext> newDraw;
        checked(newDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &newDraw));
        ComPtr<IDWriteFactory> newTextFactory;
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown**>(newTextFactory.GetAddressOf())));
        gpu = std::move(newGpu);
        composition = std::move(newComposition);
        target = std::move(newTarget);
        visual = std::move(newVisual);
        factory = std::move(newFactory);
        device = std::move(newDevice);
        draw = std::move(newDraw);
        textFactory = std::move(newTextFactory);
    }

    void render(std::wstring_view text)
    {
        initialize();
        SDL_SysWMinfo info = {};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info)) throw HRESULT(E_HANDLE);
        RECT client = {};
        if (!GetClientRect(info.info.win.window, &client)) throw HRESULT(E_HANDLE);
        const float scale = static_cast<float>(GetDpiForWindow(info.info.win.window)) / 96.0f;
        if (scale <= 0 || client.right <= 0 || client.bottom <= 0) throw HRESULT(E_INVALIDARG);
        const float width = std::min(440.0f, static_cast<float>(client.right) / scale - 16.0f);
        const float height = std::min(84.0f, static_cast<float>(client.bottom) / scale - 16.0f);
        if (width <= 32 || height <= 24) throw HRESULT(E_INVALIDARG);

        ComPtr<IDWriteTextFormat> format;
        checked(textFactory->CreateTextFormat(L"Microsoft JhengHei UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            18.0f, L"zh-TW", &format));
        checked(format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        ComPtr<IDWriteTextLayout> layout;
        checked(textFactory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                             format.Get(), width - 24, height - 16, &layout));
        const DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        checked(textFactory->CreateEllipsisTrimmingSign(format.Get(), &ellipsis));
        checked(layout->SetTrimming(&trimming, ellipsis.Get()));

        ComPtr<IDCompositionSurface> surface;
        checked(composition->CreateSurface(static_cast<UINT>(width * scale + 0.5f),
            static_cast<UINT>(height * scale + 0.5f), DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, &surface));
        ComPtr<IDXGISurface> pixels;
        POINT offset;
        checked(surface->BeginDraw(nullptr, IID_PPV_ARGS(&pixels), &offset));
        // Always close the composition draw even if D2D resource creation fails.
        HRESULT paintResult = S_OK;
        try {
            D2D1_BITMAP_PROPERTIES1 properties = {};
            properties.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
            properties.dpiX = properties.dpiY = 96.0f;
            ComPtr<ID2D1Bitmap1> bitmap;
            checked(draw->CreateBitmapFromDxgiSurface(pixels.Get(), &properties, &bitmap));
            ComPtr<ID2D1SolidColorBrush> background, foreground;
            checked(draw->CreateSolidColorBrush(D2D1::ColorF(0.04f, 0.05f, 0.07f, 0.65f), &background));
            checked(draw->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &foreground));
            draw->SetTarget(bitmap.Get());
            draw->BeginDraw();
            draw->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                               D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
            draw->Clear(D2D1::ColorF(0, 0));
            draw->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            draw->FillRectangle(D2D1::RectF(0, 0, width, height), background.Get());
            draw->DrawTextLayout(D2D1::Point2F(12, 8), layout.Get(), foreground.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            paintResult = draw->EndDraw();
            draw->SetTarget(nullptr);
        } catch (HRESULT result) { paintResult = result; }
        const HRESULT endResult = surface->EndDraw();
        checked(paintResult);
        checked(endResult);
        checked(visual->SetOffsetX(8.0f * scale));
        checked(visual->SetOffsetY(8.0f * scale));
        checked(visual->SetContent(surface.Get()));
        checked(composition->Commit());
    }
};

NativeNotification::NativeNotification(SDL_Window* window) : m_Impl(std::make_unique<Impl>(window)) {}
NativeNotification::~NativeNotification() = default;

long NativeNotification::show(std::wstring_view message, int durationMs)
{
    if (GetCurrentThreadId() != m_Impl->thread) return RPC_E_WRONG_THREAD;
    if (message.empty() || durationMs <= 0) return hide();
    if (message.size() > UINT32_MAX) return E_INVALIDARG;
    try {
        // Allocate before changing visible content so allocation failure leaves
        // the prior message and its deadline intact.
        std::wstring owned(message);
        m_Impl->render(owned);
        m_Impl->message = std::move(owned);
        m_Impl->deadline = SDL_GetTicks64() + static_cast<Uint64>(durationMs);
        m_Impl->showing = true;
        return S_OK;
    } catch (HRESULT result) { return result; }
      catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}

long NativeNotification::hide()
{
    if (GetCurrentThreadId() != m_Impl->thread) return RPC_E_WRONG_THREAD;
    try {
        if (m_Impl->composition && m_Impl->showing) {
            checked(m_Impl->visual->SetContent(nullptr));
            checked(m_Impl->composition->Commit());
        }
        m_Impl->showing = false;
        m_Impl->message.clear();
        m_Impl->deadline = 0;
        return S_OK;
    } catch (HRESULT result) { return result; }
}

long NativeNotification::refresh()
{
    if (GetCurrentThreadId() != m_Impl->thread) return RPC_E_WRONG_THREAD;
    if (!visible()) return S_OK;
    if (nextEventDelayMs() == 0) return hide();
    try { m_Impl->render(m_Impl->message); return S_OK; }
    catch (HRESULT result) { return result; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}
long NativeNotification::advance()
{
    if (GetCurrentThreadId() != m_Impl->thread) return RPC_E_WRONG_THREAD;
    return nextEventDelayMs() == 0 ? hide() : S_OK;
}
int NativeNotification::nextEventDelayMs() const
{
    if (!visible()) return -1;
    const Uint64 now = SDL_GetTicks64();
    return now >= m_Impl->deadline ? 0 : static_cast<int>(std::min<Uint64>(m_Impl->deadline - now, INT_MAX));
}
bool NativeNotification::visible() const { return m_Impl->showing; }
