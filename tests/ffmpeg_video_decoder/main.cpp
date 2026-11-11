#include "streaming/video/ffmpeg.h"
#include "streaming/video/decoderfactory.h"
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
std::string readFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!bytes.empty(), "read production resource");
    return bytes;
}
class Host final : public IFFmpegDecoderHost {
public:
    explicit Host(std::string root) : root(std::move(root)), overlays(readFile(this->root + "/app/ModeSeven.ttf")) {}
    std::string root;
    Overlay::OverlayManager overlays;
    int shaderRequests = 0;
    void flushWindowEvents() override { SDL_PumpEvents(); SDL_FlushEvent(SDL_WINDOWEVENT); }
    Overlay::OverlayManager& getOverlayManager() override { return overlays; }
    bool isOverlayEnabled(Overlay::OverlayType type) override { return overlays.isOverlayEnabled(type); }
    SDL_Surface* getUpdatedOverlaySurface(Overlay::OverlayType type) override { return overlays.getUpdatedOverlaySurface(type); }
    std::string readShader(const char* name) override
    {
        ++shaderRequests;
        return readFile(root + "/app/shaders/" + name);
    }
    int getCurrentBandwidthKbps() override { return 12345; }
    bool isGpuSlow() override { return false; }
};
void probeDecoders(Host& host)
{
    require(_putenv_s("D3D11VA_ENABLED", "1") == 0, "enable hardware decoder probe");
    require(_putenv_s("D3D11VA_DEBUG_LAYER", "0") == 0, "avoid optional Graphics Tools dependency");
    require(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, SDL_GetError());
    SDL_Window* window = SDL_CreateWindow("Native FFmpeg decoder probe", 0, 0, 640, 360,
                                          SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
    require(window != nullptr, SDL_GetError());
    for (auto choice : {VideoDecoderOptions::Software, VideoDecoderOptions::Hardware}) {
        for (int format : {VIDEO_FORMAT_H264, VIDEO_FORMAT_H265, VIDEO_FORMAT_AV1_MAIN8}) {
            IVideoDecoder* selected = nullptr;
            require(VideoDecoderFactory::choose(&host, choice, VideoDecoderOptions::Automatic,
                        window, format, 640, 360, 60,
                        false, false, false, false, true, selected),
                    "production factory sample decode and renderer mapping");
            std::unique_ptr<IVideoDecoder> decoder(selected);
            require(decoder->isHardwareAccelerated() == (choice == VideoDecoderOptions::Hardware),
                    "retain explicit software/hardware selection");
            require(decoder->getDecoderCapabilities() & CAPABILITY_PULL_RENDERER, "retain pull renderer capability");
            std::printf("production decoder probe passed: format=%x hardware=%d\n", format, decoder->isHardwareAccelerated());
        }
    }
    bool hardware = false, fullscreenOnly = false, hdr = false;
    VideoResolution maximum{-1, -1};
    VideoDecoderFactory::getInfo(&host, window, hardware, fullscreenOnly, hdr, maximum);
    require(hardware && maximum.width >= 0 && maximum.height >= 0,
            "production capability probing finds available hardware and reports resolution limits");
    require(host.shaderRequests > 0, "production hardware path obtains shaders from native owner");
    SDL_DestroyWindow(window);
}
}
int main(int argc, char** argv)
{
    try {
        require(argc == 2 || (argc == 3 && std::string(argv[2]) == "--probe"),
                "usage: ffmpeg_video_decoder repository-root [--probe]");
        SDL_SetMainReady();
        SDL_SetAssertionHandler([](const SDL_AssertData*, void*) -> SDL_AssertState { std::_Exit(2); }, nullptr);
        require(SDL_Init(0) == 0 && TTF_Init() == 0, "initialize native SDL/TTF");
        {
            Host host(argv[1]);
            { FFmpegVideoDecoder decoder(true, host); }
            { FFmpegVideoDecoder decoder(false, host); }
            require(host.shaderRequests == 0, "uninitialized decoder does not request GPU resources");
            if (argc == 3) probeDecoders(host);
        }
        TTF_Quit();
        SDL_Quit();
        std::puts("Native FFmpeg decoder checks passed.");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        SDL_Quit();
        return 1;
    }
}
