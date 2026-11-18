#define NOMINMAX
#include <SDL.h>
#include <SDL_syswm.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include "streaming/video/nativenotification.h"
using Microsoft::WRL::ComPtr;
void check(HRESULT result, const char* what) {
    if (FAILED(result)) { std::fprintf(stderr, "%s: %08lx\n", what, result); throw std::runtime_error(what); }
}
int main(int argc, char** argv) {
    try {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        const bool fullscreen = mode == "--fullscreen" || mode == "--hdr";
        const bool hdr = mode == "--hdr";
        const bool visible = mode == "--visible" || fullscreen;
        if (argc > 2 || (argc == 2 && !visible)) {
            throw std::runtime_error("Usage: native_notification [--visible | --fullscreen | --hdr]");
        }
        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
        if (SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } } sdlLifetime;
        const Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP :
                             visible ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN;
        const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> windowOwner{
            SDL_CreateWindow("Native notification capability probe", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, 800, 450, flags), SDL_DestroyWindow};
        SDL_Window* window = windowOwner.get();
        if (!window) throw std::runtime_error(SDL_GetError());
        SDL_SysWMinfo info = {};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info)) throw std::runtime_error(SDL_GetError());
        const HWND hwnd = info.info.win.window;
        {
            NativeNotification notification(window);
            ComPtr<ID3D11Device> video;
            ComPtr<ID3D11DeviceContext> context;
            check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &video, nullptr, &context), "simulated video device");
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            check(video.As(&dxgi), "video DXGI");
            check(dxgi->GetAdapter(&adapter), "video adapter");
            check(adapter->GetParent(IID_PPV_ARGS(&factory)), "video factory");
            DXGI_SWAP_CHAIN_DESC1 description = {};
            // Zero dimensions let DXGI use the actual fullscreen client size.
            description.Format = hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = 2;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            ComPtr<IDXGISwapChain1> swap;
            check(factory->CreateSwapChainForHwnd(video.Get(), hwnd, &description, nullptr, nullptr, &swap), "same-window video swapchain");
            if (visible) {
                ComPtr<IDXGIOutput> output;
                ComPtr<IDXGIOutput6> output6;
                DXGI_OUTPUT_DESC1 display{};
                check(swap->GetContainingOutput(&output), "probe output");
                check(output.As(&output6), "probe HDR output interface");
                check(output6->GetDesc1(&display), "probe active output configuration");
                const bool fullscreenActive = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) ==
                                              SDL_WINDOW_FULLSCREEN_DESKTOP;
                std::printf("fullscreen=%d dpi=%u output_colorspace=%u\n", fullscreenActive,
                            GetDpiForWindow(hwnd), static_cast<unsigned>(display.ColorSpace));
                if (fullscreen && !fullscreenActive) throw std::runtime_error("Fullscreen mode unavailable");
                if (hdr) {
                    constexpr auto colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    if (display.ColorSpace != colorSpace) {
                        std::fputs("SKIP: selected display is not running in HDR mode\n", stderr);
                        return 77;
                    }
                    ComPtr<IDXGISwapChain3> hdrSwap;
                    check(swap.As(&hdrSwap), "HDR swapchain interface");
                    check(hdrSwap->SetColorSpace1(colorSpace), "enable HDR10 presentation");
                }
            }
            ComPtr<ID3D11Texture2D> frame;
            ComPtr<ID3D11RenderTargetView> view;
            check(swap->GetBuffer(0, IID_PPV_ARGS(&frame)), "video buffer");
            check(video->CreateRenderTargetView(frame.Get(), nullptr, &view), "video target");
            const float blue[] = {0.05f, 0.35f, 0.8f, 1};
            context->ClearRenderTargetView(view.Get(), blue);
            check(swap->Present(0, 0), "initial video frame");
            if (notification.visible() || notification.nextEventDelayMs() != -1) throw std::runtime_error("initially hidden");
            check(notification.hide(), "hide before initialization");
            long wrongThread = S_OK;
            std::thread foreign([&] { wrongThread = notification.show(L"Foreign thread", 100); });
            foreign.join();
            if (wrongThread != RPC_E_WRONG_THREAD || notification.visible()) throw std::runtime_error("foreign thread accepted");
            const Uint32 interval = fullscreen ? 700 : visible ? 5000 : 25;
            const Uint32 start = SDL_GetTicks();
            int phase = -1;
            bool quit = false;
            unsigned framesPresented = 0;
            while (!quit) {
                Uint32 elapsed = SDL_GetTicks() - start;
                int current = static_cast<int>(elapsed / interval);
                if (current >= 5) break;
                if (current != phase) {
                    if (fullscreen && (phase == 0 || phase == 1)) {
                        if (framesPresented == 0) throw std::runtime_error("No frames presented during fullscreen phase");
                        std::printf("notification_%s: frames_presented=%u\n", phase == 0 ? "visible" : "hidden",
                                    framesPresented);
                    }
                    framesPresented = 0;
                    const HWND before = GetForegroundWindow();
                    if (current == 0) check(notification.show(L"重新連線中…（還剩 60 秒）", 60000), "show Chinese");
                    if (current == 1) check(notification.hide(), "hide");
                    if (current == 2) {
                        view.Reset(); frame.Reset(); swap.Reset(); context.Reset(); video.Reset();
                        factory.Reset(); adapter.Reset(); dxgi.Reset();
                        check(notification.show(L"解碼裝置已釋放，通知仍可更新", 60000), "reshow without video device");
                    }
                    if (current == 3) check(notification.show(L"已重新連線，即將隱藏通知", static_cast<int>(interval / 2)), "replace message");
                    if (current == 4) {
                        check(notification.advance(), "expire notification");
                        if (notification.visible() || notification.nextEventDelayMs() != -1) throw std::runtime_error("expired notification remains visible");
                    }
                    if (current == 2 && !fullscreen) {
                        SDL_SetWindowSize(window, 600, 320);
                        check(notification.refresh(), "refresh resized notification");
                    }
                    if (GetForegroundWindow() != before) throw std::runtime_error("notification changed foreground window");
                    phase = current;
                    std::printf("phase=%d foreground unchanged\n", phase);
                    std::fflush(stdout);
                }
                if (fullscreen && swap) {
                    context->ClearRenderTargetView(view.Get(), blue);
                    const auto result = swap->Present(1, 0);
                    check(result, "sustained video present");
                    if (result == DXGI_STATUS_OCCLUDED) throw std::runtime_error("Fullscreen probe is occluded");
                    ++framesPresented;
                }
                SDL_Event event;
                const Uint32 elapsedNow = SDL_GetTicks() - start;
                const Uint32 nextPhase = (phase + 1) * interval;
                const int wait = fullscreen && swap ? 0 :
                                 nextPhase > elapsedNow ? static_cast<int>(nextPhase - elapsedNow) : 1;
                if (SDL_WaitEventTimeout(&event, wait)) {
                    if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) quit = true;
                }
            }
            if (fullscreen && phase != 4) throw std::runtime_error("Fullscreen probe stopped early");
        }
        {
            NativeNotification replacement(window);
            check(replacement.show(L"重新建立通知區", 100), "recreate after target destruction");
            check(replacement.show(L"", 100), "empty message hides");
            if (replacement.visible()) throw std::runtime_error("empty message remains visible");
            check(replacement.show(L"零期限", 0), "zero duration hides");
        }
        {
            NativeNotification invalid(nullptr);
            if (SUCCEEDED(invalid.show(L"No window", 100))) throw std::runtime_error("invalid owner accepted");
            check(invalid.hide(), "invalid owner cleanup");
        }
        std::puts("Notification capability probe finished.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
