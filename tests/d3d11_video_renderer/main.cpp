#include "streaming/video/ffmpeg-renderers/d3d11va.h"
#include "streaming/environment.h"
#include "streaming/windowsstreamassets.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <set>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct Host final : ID3D11RendererHost {
    int requests = 0;
    std::string readShader(const char*) override { ++requests; return {}; }
    bool isOverlayEnabled(Overlay::OverlayType) override { ++requests; return false; }
    SDL_Surface* getUpdatedOverlaySurface(Overlay::OverlayType) override { ++requests; return nullptr; }
};
struct FileHost final : ID3D11RendererHost {
    std::string directory;
    std::set<std::string> shaders;
    std::string readShader(const char* name) override
    {
        std::ifstream input(directory + "/" + name, std::ios::binary);
        const std::string expected{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        auto bytes = readWindowsStreamAsset(name, {}, {}, {}, {});
        require(!bytes.empty() && bytes == expected, "embedded production shader matches source bytes");
        shaders.insert(name);
        return bytes;
    }
    bool isOverlayEnabled(Overlay::OverlayType) override { return false; }
    SDL_Surface* getUpdatedOverlaySurface(Overlay::OverlayType) override { return nullptr; }
};
void verifyDevice(const char* directory)
{
    require(_putenv_s("D3D11VA_ENABLED", "1") == 0, "enable explicit device probe");
    require(_putenv_s("D3D11VA_DEBUG_LAYER", "0") == 0, "probe does not require optional Graphics Tools");
    require(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, "initialize video");
    SDL_Window* window = SDL_CreateWindow("Native D3D11 probe", SDL_WINDOWPOS_UNDEFINED,
                                         SDL_WINDOWPOS_UNDEFINED, 640, 360, SDL_WINDOW_HIDDEN);
    require(window != nullptr, "create hidden probe window");
    FileHost host;
    host.directory = directory;
    {
        D3D11VARenderer renderer(0, host);
        DECODER_PARAMETERS parameters = {};
        parameters.window = window;
        parameters.width = 1280;
        parameters.height = 720;
        parameters.frameRate = 60;
        parameters.videoFormat = VIDEO_FORMAT_H264;
        parameters.testOnly = true;
        require(renderer.initialize(&parameters), "initialize actual D3D11 video device and shaders");
        require(host.shaders == std::set<std::string>{"d3d11_vertex.fxc", "d3d11_overlay_pixel.fxc",
                    "d3d11_yuv420_pixel.fxc", "d3d11_ayuv_pixel.fxc", "d3d11_y410_pixel.fxc"},
                "production renderer requests all five shader resources");
        AVCodecContext* context = avcodec_alloc_context3(nullptr);
        require(context != nullptr, "allocate decoder context");
        require(renderer.prepareDecoderContext(context, nullptr) && context->hw_device_ctx != nullptr,
                "attach real hardware context to FFmpeg");
        avcodec_free_context(&context);
        for (int type = 0; type < Overlay::OverlayMax; ++type) {
            renderer.notifyOverlayUpdated(static_cast<Overlay::OverlayType>(type));
        }
    }
    SDL_DestroyWindow(window);
    std::puts("Native D3D11 device, five production shaders and FFmpeg hardware context passed; video frames not tested.");
}
void verifyOverrides()
{
    struct Case { const char* text; bool valid; int value; };
    const Case cases[] = {
        {nullptr, false, 0}, {"", false, 0}, {" \t", false, 0},
        {"0", true, 0}, {"-0", true, 0}, {"00", true, 0},
        {"1", true, 1}, {"-1", true, -1}, {"+42", true, 42},
        {"  \t-42\r\n", true, -42}, {"012", true, 10}, {"08", false, 0},
        {"0x10", true, 16}, {"-0X10", true, -16}, {"0b10", true, 2},
        {"+0B10", true, 2}, {"0x", false, 0}, {"0b2", false, 0},
        {"+", false, 0}, {"--1", false, 0}, {"1 2", false, 0}, {"1x", false, 0},
        {"2147483647", true, 2147483647}, {"-2147483648", true, (-2147483647 - 1)},
        {"2147483648", false, 0}, {"-2147483649", false, 0},
        {"0x7fffffff", true, 2147483647}, {"-0x80000000", true, (-2147483647 - 1)},
        {"999999999999999999999999999", false, 0},
    };
    for (const auto& item : cases) {
        int result = 99;
        const bool valid = StreamEnvironment::parseInteger(item.text, &result);
        if (valid != item.valid || result != item.value) {
            std::fprintf(stderr, "override mismatch: %s\n", item.text ? item.text : "<unset>");
            throw std::runtime_error("override parsing");
        }
    }
    require(_putenv_s("MOONLIGHT_D3D11_TEST_OVERRIDE", "-2") == 0, "set isolated override");
    bool boolean = false;
    require(StreamEnvironment::getOverride("MOONLIGHT_D3D11_TEST_OVERRIDE", &boolean) && boolean,
            "nonzero signed override converts to true");
    require(_putenv_s("MOONLIGHT_D3D11_TEST_OVERRIDE", "invalid") == 0, "set invalid override");
    boolean = true;
    require(!StreamEnvironment::getOverride("MOONLIGHT_D3D11_TEST_OVERRIDE", &boolean) && !boolean,
            "invalid override clears output");
}
}

int main(int argc, char** argv)
{
    try {
        SDL_SetMainReady();
        require(SDL_Init(0) == 0, "initialize SDL without opening a window");
        verifyOverrides();
        require(_putenv_s("D3D11VA_ENABLED", "0") == 0, "disable renderer in test process");
        Host host;
        for (int pass = 0; pass < 2; ++pass) {
            D3D11VARenderer renderer(pass, host);
            DECODER_PARAMETERS parameters = {};
            require(!renderer.initialize(&parameters), "environment disable preserves fallback");
            require(renderer.getRendererType() == IFFmpegRenderer::RendererType::D3D11VA,
                    "production renderer identity");
            require(renderer.getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT,
                    "HDR capability remains exposed");
        }
        require(host.requests == 0, "disabled renderer does not request shaders or overlay data");
        if (argc == 3 && std::string(argv[1]) == "--probe") verifyDevice(argv[2]);
        else require(argc == 1, "usage: d3d11_video_renderer [--probe shader-directory]");
        SDL_Quit();
        std::puts("Native D3D11: production link, overrides and disabled fallback passed; GPU rendering not tested.");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s; SDL: %s\n", error.what(), SDL_GetError());
        return 1;
    }
}
