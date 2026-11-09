#define NOMINMAX
#include <SDL.h>
#include <SDL_syswm.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include "streaming/video/nativenotification.h"
using Microsoft::WRL::ComPtr;
void check(HRESULT result, const char* what) {
    if (FAILED(result)) { std::fprintf(stderr, "%s: %08lx\n", what, result); throw std::runtime_error(what); }
}
int main(int argc, char** argv) {
    try {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_Window* window = SDL_CreateWindow("Native notification capability probe", SDL_WINDOWPOS_CENTERED,
                                              SDL_WINDOWPOS_CENTERED, 800, 450, (argc == 2 && std::string(argv[1]) == "--visible") ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
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
            description.Width = 800; description.Height = 450;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = 2;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            ComPtr<IDXGISwapChain1> swap;
            check(factory->CreateSwapChainForHwnd(video.Get(), hwnd, &description, nullptr, nullptr, &swap), "same-window video swapchain");
            ComPtr<ID3D11Texture2D> frame;
            ComPtr<ID3D11RenderTargetView> view;
            check(swap->GetBuffer(0, IID_PPV_ARGS(&frame)), "video buffer");
            check(video->CreateRenderTargetView(frame.Get(), nullptr, &view), "video target");
            const float blue[] = {0.05f, 0.35f, 0.8f, 1};
            context->ClearRenderTargetView(view.Get(), blue);
            check(swap->Present(0, 0), "single video frame");
            if (notification.visible() || notification.nextEventDelayMs() != -1) throw std::runtime_error("initially hidden");
            check(notification.hide(), "hide before initialization");
            long wrongThread = S_OK;
            std::thread foreign([&] { wrongThread = notification.show(L"Foreign thread", 100); });
            foreign.join();
            if (wrongThread != RPC_E_WRONG_THREAD || notification.visible()) throw std::runtime_error("foreign thread accepted");
            const bool visible = argc == 2 && std::string(argv[1]) == "--visible";
            const Uint32 interval = visible ? 5000 : 25;
            const Uint32 start = SDL_GetTicks();
            int phase = -1;
            bool quit = false;
            while (!quit) {
                Uint32 elapsed = SDL_GetTicks() - start;
                int current = static_cast<int>(elapsed / interval);
                if (current >= 5) break;
                if (current != phase) {
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
                    if (current == 2) {
                        SDL_SetWindowSize(window, 600, 320);
                        check(notification.refresh(), "refresh resized notification");
                    }
                    if (GetForegroundWindow() != before) throw std::runtime_error("notification changed foreground window");
                    phase = current;
                    std::printf("phase=%d foreground unchanged; video presents=1\n", phase);
                    std::fflush(stdout);
                }
                SDL_Event event;
                const Uint32 elapsedNow = SDL_GetTicks() - start;
                const Uint32 nextPhase = (phase + 1) * interval;
                if (SDL_WaitEventTimeout(&event, nextPhase > elapsedNow ? static_cast<int>(nextPhase - elapsedNow) : 1)) {
                    if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) quit = true;
                }
            }
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
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::puts("Notification capability probe finished (no streaming host or product changes).");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
