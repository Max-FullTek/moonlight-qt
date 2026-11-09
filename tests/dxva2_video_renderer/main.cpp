#define NOMINMAX
#include "streaming/video/ffmpeg-renderers/dxva2.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
bool loggedError = false;
void SDLCALL logOutput(void*, int, SDL_LogPriority priority, const char* message)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR) loggedError = true;
    std::fprintf(stderr, "%s\n", message);
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class Host final : public IDxva2RendererHost {
public:
    int requests = 0;
    bool enabled = false;
    SDL_Surface* pending = nullptr;
    ~Host() override { SDL_FreeSurface(pending); }
    bool isOverlayEnabled(Overlay::OverlayType) override { ++requests; return enabled; }
    SDL_Surface* getUpdatedOverlaySurface(Overlay::OverlayType) override
    {
        ++requests;
        return std::exchange(pending, nullptr);
    }
};

void probeDevice()
{
    require(_putenv_s("DXVA2_ENABLED", "1") == 0, "enable explicit GPU probe");
    require(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, SDL_GetError());
    SDL_Window* window = SDL_CreateWindow("DXVA2 native device probe", 0, 0, 640, 360, SDL_WINDOW_HIDDEN);
    require(window != nullptr, SDL_GetError());
    {
        Host host;
        DXVA2Renderer renderer(0, host);
        DECODER_PARAMETERS parameters = {};
        parameters.window = window;
        parameters.width = 640;
        parameters.height = 360;
        parameters.frameRate = 60;
        parameters.videoFormat = VIDEO_FORMAT_H264;
        require(renderer.initialize(&parameters), "initialize production DXVA2 device and processor");
        AVCodecContext* context = avcodec_alloc_context3(nullptr);
        require(context != nullptr, "allocate FFmpeg decoder context");
        require(renderer.prepareDecoderContext(context, nullptr) && context->hw_device_ctx != nullptr,
                "production DXVA2 hardware context");
        avcodec_free_context(&context);
        renderer.notifyOverlayUpdated(Overlay::OverlayDebug);
        require(host.requests == 2, "disabled overlay routes through owner");
        host.enabled = true;
        host.pending = SDL_CreateRGBSurfaceWithFormat(0, 48, 24, 32, SDL_PIXELFORMAT_ARGB8888);
        require(host.pending != nullptr, "allocate owner overlay surface");
        require(SDL_FillRect(host.pending, nullptr, 0x80ffffff) == 0, "fill overlay surface");
        renderer.notifyOverlayUpdated(Overlay::OverlayDebug);
        require(host.pending == nullptr && host.requests == 4, "new surface transfers to production renderer");
        renderer.notifyOverlayUpdated(Overlay::OverlayDebug);
        require(host.requests == 6, "enabled overlay without new pixels preserves texture");
        host.enabled = false;
        renderer.notifyOverlayUpdated(Overlay::OverlayDebug);
        require(host.requests == 8, "owner disable clears overlay resources");
        require(!loggedError, "production device and overlay operations did not log errors");
    }
    SDL_DestroyWindow(window);
}
}

int main(int argc, char** argv)
{
    try {
        SDL_SetMainReady();
        SDL_LogSetOutputFunction(logOutput, nullptr);
        require(SDL_Init(0) == 0, "initialize SDL without video");
        require(_putenv_s("DXVA2_ENABLED", "0") == 0, "disable renderer in test process");
        Host host;
        for (int pass = 0; pass < 2; ++pass) {
            DXVA2Renderer renderer(pass, host);
            DECODER_PARAMETERS parameters = {};
            require(!renderer.initialize(&parameters), "explicit disable preserves fallback");
            require(renderer.getRendererType() == IFFmpegRenderer::RendererType::DXVA2, "renderer identity");
            require(!(renderer.getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT), "DXVA2 does not advertise HDR");
        }
        require(host.requests == 0, "disabled renderer does not request overlay data");
        require(_putenv_s("DXVA2_ENABLED", "1") == 0, "enable HDR fallback test");
        {
            DXVA2Renderer renderer(0, host);
            DECODER_PARAMETERS parameters = {};
            parameters.videoFormat = VIDEO_FORMAT_H265_MAIN10;
            require(!renderer.initialize(&parameters), "first pass preserves HDR fallback before device creation");
        }
        if (argc == 2 && std::string(argv[1]) == "--probe") probeDevice();
        else require(argc == 1, "usage: dxva2_video_renderer [--probe]");
        SDL_Quit();
        std::puts("Native DXVA2 production linkage and fallback passed.");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        SDL_Quit();
        return 1;
    }
}
