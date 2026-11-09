#define NOMINMAX
#include "streaming/video/ffmpeg-renderers/plvk.h"
#include "streaming/video/ffmpeg-renderers/genhwaccel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
class Host final : public IPlVkRendererHost {
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
    require(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, SDL_GetError());
    SDL_Window* window = SDL_CreateWindow("Native Vulkan probe", 0, 0, 640, 360,
                                          SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
    require(window != nullptr, SDL_GetError());
    {
        Host host;
        PlVkRenderer renderer(host);
        DECODER_PARAMETERS parameters = {};
        parameters.window = window;
        parameters.width = 640;
        parameters.height = 360;
        parameters.frameRate = 60;
        parameters.videoFormat = VIDEO_FORMAT_H264;
        require(renderer.initialize(&parameters), "initialize production Vulkan renderer");
        AVFrame* frame = av_frame_alloc();
        require(frame != nullptr, "allocate test frame");
        frame->format = AV_PIX_FMT_YUV420P;
        frame->width = 640;
        frame->height = 360;
        frame->color_range = AVCOL_RANGE_MPEG;
        frame->colorspace = AVCOL_SPC_BT709;
        require(av_frame_get_buffer(frame, 32) == 0, "allocate planar pixels");
        for (int plane = 0; plane < 3; ++plane) {
            std::memset(frame->data[plane], plane ? 128 : 80,
                        frame->linesize[plane] * (plane ? 180 : 360));
        }
        require(renderer.testRenderFrame(frame), "production libplacebo frame upload");
        host.enabled = true;
        host.pending = SDL_CreateRGBSurfaceWithFormat(0, 48, 24, 32, SDL_PIXELFORMAT_ARGB8888);
        require(host.pending != nullptr, "allocate overlay surface");
        require(SDL_FillRect(host.pending, nullptr, 0x80ffffff) == 0, "fill overlay pixels");
        renderer.notifyOverlayUpdated(Overlay::OverlayDebug);
        require(host.pending == nullptr, "overlay surface ownership transfer");
        renderer.notifyOverlayUpdated(Overlay::OverlayDebug);
        renderer.waitToRender();
        renderer.renderFrame(frame);
        host.enabled = false;
        renderer.notifyOverlayUpdated(Overlay::OverlayDebug);
        renderer.waitToRender();
        renderer.renderFrame(frame);
        renderer.cleanupRenderContext();
        av_frame_free(&frame);
        require(host.requests >= 5, "overlay state queried from native owner");
        require(!loggedError, "Vulkan upload and presentation did not log errors");
    }
    SDL_DestroyWindow(window);
    {
        GenericHwAccelRenderer renderer(AV_HWDEVICE_TYPE_D3D11VA);
        DECODER_PARAMETERS parameters = {};
        require(renderer.initialize(&parameters), "generic D3D11 hardware context");
        AVCodecContext* context = avcodec_alloc_context3(nullptr);
        require(context != nullptr, "allocate FFmpeg codec context");
        require(renderer.prepareDecoderContext(context, nullptr) && context->hw_device_ctx,
                "generic backend transfers hardware context reference");
        avcodec_free_context(&context);
    }
}
}

int main(int argc, char** argv)
{
    try {
        SDL_SetMainReady();
        SDL_LogSetOutputFunction(logOutput, nullptr);
        SDL_SetAssertionHandler([](const SDL_AssertData*, void*) -> SDL_AssertState { std::_Exit(2); }, nullptr);
        require(SDL_Init(0) == 0, "initialize SDL without video");
        Host host;
        {
            PlVkRenderer renderer(host);
            require(renderer.getRendererType() == IFFmpegRenderer::RendererType::Vulkan, "Vulkan renderer identity");
            require(renderer.getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT, "retain HDR support");
            WINDOW_STATE_CHANGE_INFO change = {};
            change.stateChangeFlags = WINDOW_STATE_CHANGE_SIZE | WINDOW_STATE_CHANGE_DISPLAY;
            require(renderer.notifyWindowChanged(&change), "retain size/display updates");
            change.stateChangeFlags = 0x80000000;
            require(!renderer.notifyWindowChanged(&change), "reject unknown window state flags");
        }
        require(host.requests == 0, "pre-device capability queries do not request overlays");
        GenericHwAccelRenderer generic(AV_HWDEVICE_TYPE_NONE);
        require(!generic.isDirectRenderingSupported(), "generic backend retains read-back rendering");
        require(_putenv_s("GENHWACCEL_CAPS", "0x1234") == 0, "set capability override");
        require(generic.getDecoderCapabilities() == 0x1234, "native capability override");
        require(_putenv_s("GENHWACCEL_CAPS", "invalid") == 0, "invalidate override");
        require(generic.getDecoderCapabilities() == 0, "invalid override uses original default");
        require(_putenv_s("GENHWACCEL_CAPS", "") == 0, "remove override");
        require(generic.getDecoderCapabilities() == 0, "missing override uses original default");
        if (argc == 2 && std::string(argv[1]) == "--probe") probeDevice();
        else require(argc == 1, "usage: vulkan_video_renderer [--probe]");
        SDL_Quit();
        std::puts("Native Vulkan and generic hwaccel checks passed.");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        SDL_Quit();
        return 1;
    }
}
