#include "streaming/video/ffmpeg-renderers/sdlvid.h"
#include "streaming/video/ffmpeg-renderers/pacer/pacer.h"
#include "streaming/video/decodebuffer.h"
#include "../../app/streaming/video/ffmpeg_videosamples.cpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {
SDL_LogOutputFunction savedOutput = nullptr;
void* savedOutputContext = nullptr;
std::atomic<int> errorLogs{0};
void SDLCALL reportLog(void*, int category, SDL_LogPriority priority, const char* message)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR) ++errorLogs;
    if (savedOutput != nullptr) savedOutput(savedOutputContext, category, priority, message);
}

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s; SDL: %s\n", message, SDL_GetError());
        std::exit(1);
    }
}
SDL_AssertState SDLCALL reportAssertion(const SDL_AssertData* data, void*)
{
    std::fprintf(stderr, "SDL assertion: %s (%s:%d)\n", data->condition, data->filename, data->linenum);
    std::_Exit(2);
}
class Host : public ISdlRendererHost {
public:
    void flushWindowEvents() override { ++flushes; }
    bool isOverlayEnabled(Overlay::OverlayType type) override { return enabled[type]; }
    SDL_Surface* getUpdatedOverlaySurface(Overlay::OverlayType type) override
    {
        ++queries[type];
        auto* result = surfaces[type];
        surfaces[type] = nullptr;
        return result;
    }
    int flushes = 0;
    std::array<bool, Overlay::OverlayMax> enabled{};
    std::array<int, Overlay::OverlayMax> queries{};
    std::array<SDL_Surface*, Overlay::OverlayMax> surfaces{};
};
void probe()
{
    Host host;
    SdlRenderer renderer(&host);
    DECODER_PARAMETERS params{};
    params.testOnly = true;
    params.videoFormat = VIDEO_FORMAT_H264;
    require(renderer.initialize(&params), "probe works without a window or initialized SDL video");
    require(host.flushes == 0, "probe never pumps or flushes window events");
    for (AVPixelFormat format : {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_NV12, AV_PIX_FMT_NV21}) {
        require(renderer.isPixelFormatSupported(VIDEO_FORMAT_H264, format), "native 8-bit 4:2:0 formats supported");
    }
    require(!renderer.isPixelFormatSupported(VIDEO_FORMAT_H264, AV_PIX_FMT_YUV444P), "4:2:0 mode rejects 4:4:4 pixels");
    require(renderer.isPixelFormatSupported(VIDEO_FORMAT_H264_HIGH8_444, AV_PIX_FMT_YUV444P), "4:4:4 8-bit conversion supported");
    require(!renderer.isPixelFormatSupported(VIDEO_FORMAT_H264_HIGH8_444, AV_PIX_FMT_YUV420P), "4:4:4 mode rejects 4:2:0 pixels");
    require(renderer.isPixelFormatSupported(VIDEO_FORMAT_H265_MAIN10, AV_PIX_FMT_P010), "10-bit layout recognized for readback");
    params.videoFormat = VIDEO_FORMAT_H265_MAIN10;
    require(!renderer.initialize(&params), "SDL renderer still rejects HDR selection");
    require(renderer.getDecoderColorspace() == COLORSPACE_REC_601 &&
            renderer.getDecoderColorRange() == COLOR_RANGE_FULL, "preserve SDL color negotiation");
    WINDOW_STATE_CHANGE_INFO change{};
    change.stateChangeFlags = WINDOW_STATE_CHANGE_SIZE | WINDOW_STATE_CHANGE_DISPLAY;
    require(renderer.notifyWindowChanged(&change), "size and display changes handled without recreation");
    change.stateChangeFlags = 4;
    require(!renderer.notifyWindowChanged(&change), "unknown window changes require recreation");
}

AVFrame* grayFrame(AVPixelFormat format, int width, int height, uint8_t y)
{
    AVFrame* frame = av_frame_alloc();
    require(frame != nullptr, "allocate video frame");
    frame->format = format;
    frame->width = width;
    frame->height = height;
    frame->colorspace = AVCOL_SPC_SMPTE170M;
    frame->color_range = AVCOL_RANGE_MPEG;
    require(av_frame_get_buffer(frame, 32) == 0, "allocate YUV pixels");
    for (int row = 0; row < height; ++row) {
        std::memset(frame->data[0] + row * frame->linesize[0], y, width);
    }
    if (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUV444P) {
        int chromaWidth = format == AV_PIX_FMT_YUV444P ? width : width / 2;
        int chromaHeight = format == AV_PIX_FMT_YUV444P ? height : height / 2;
        for (int plane = 1; plane <= 2; ++plane) {
            for (int row = 0; row < chromaHeight; ++row) {
                std::memset(frame->data[plane] + row * frame->linesize[plane], 128, chromaWidth);
            }
        }
    }
    else {
        for (int row = 0; row < height / 2; ++row) {
            std::memset(frame->data[1] + row * frame->linesize[1], 128, width);
        }
    }
    return frame;
}

void expectColor(SDL_Renderer* renderer, int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
    SDL_Rect area{x, y, 1, 1};
    uint8_t rgba[4]{};
    require(SDL_RenderReadPixels(renderer, &area, SDL_PIXELFORMAT_RGBA32, rgba, 4) == 0,
            "read actual rendered pixel");
    require(std::abs(static_cast<int>(rgba[0]) - red) <= 2 &&
            std::abs(static_cast<int>(rgba[1]) - green) <= 2 &&
            std::abs(static_cast<int>(rgba[2]) - blue) <= 2, "rendered pixel matches expected color");
}
SDL_Surface* overlay(uint8_t red, uint8_t green, uint8_t blue)
{
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 32, 16, 32, SDL_PIXELFORMAT_ARGB8888);
    require(surface != nullptr && SDL_FillRect(surface, nullptr,
            SDL_MapRGBA(surface->format, red, green, blue, 255)) == 0, "create colored overlay surface");
    return surface;
}

class CountingRenderer : public SdlRenderer {
public:
    explicit CountingRenderer(ISdlRendererHost* host) : SdlRenderer(host) {}
    void renderFrame(AVFrame* frame) override
    {
        SdlRenderer::renderFrame(frame);
        std::lock_guard<std::mutex> guard(lock);
        rendered = true;
        changed.notify_one();
    }
    void waitForFrame()
    {
        std::unique_lock<std::mutex> guard(lock);
        require(changed.wait_for(guard, std::chrono::seconds(2), [&] { return rendered; }),
                "production pacer delivers decoded frame to actual SDL renderer");
    }
    std::mutex lock;
    std::condition_variable changed;
    bool rendered = false;
};

template<size_t N>
void decodeAndPresent(SDL_Window* window, AVCodecID codecId, int videoFormat, const uint8_t (&sample)[N])
{
    LENTRY entries[2]{};
    entries[0].data = reinterpret_cast<char*>(const_cast<uint8_t*>(sample));
    entries[0].length = static_cast<int>(N / 2);
    entries[0].next = &entries[1];
    entries[1].data = entries[0].data + entries[0].length;
    entries[1].length = static_cast<int>(N) - entries[0].length;
    DECODE_UNIT unit{};
    unit.bufferList = entries;
    unit.fullLength = static_cast<int>(N);
    unit.frameType = FRAME_TYPE_IDR;
    VideoDecodeBuffer buffer;
    const int length = buffer.assemble(&unit, false, codecId == AV_CODEC_ID_AV1);
    AVCodecContext* context = avcodec_alloc_context3(avcodec_find_decoder(codecId));
    require(context != nullptr, "allocate actual software decoder");
    context->thread_count = 1;
    require(avcodec_open2(context, nullptr, nullptr) == 0, "open actual software decoder");
    AVPacket* packet = av_packet_alloc();
    require(packet != nullptr, "allocate actual decoder packet");
    packet->data = buffer.data();
    packet->size = length;
    require(avcodec_send_packet(context, packet) == 0, "send fragmented production sample");
    AVFrame* frame = av_frame_alloc();
    require(frame != nullptr, "allocate actual decoder output");
    int result = avcodec_receive_frame(context, frame);
    if (result == AVERROR(EAGAIN)) {
        require(avcodec_send_packet(context, nullptr) == 0, "flush delayed production sample");
        result = avcodec_receive_frame(context, frame);
    }
    require(result == 0, "decode production sample for actual presentation");
    av_packet_free(&packet);
    avcodec_free_context(&context);
    Host host;
    CountingRenderer renderer(&host);
    DECODER_PARAMETERS params{};
    params.window = window;
    params.videoFormat = videoFormat;
    params.width = 1280;
    params.height = 720;
    params.frameRate = 60;
    require(renderer.initialize(&params) && renderer.testRenderFrame(frame), "negotiate decoded sample with actual renderer");
    renderer.prepareToRender();
    VIDEO_STATS stats{};
    {
        Pacer pacer(&renderer, &stats);
        require(pacer.initialize(window, 60, false), "initialize actual presentation pacer");
        frame->pkt_dts = LiGetMicroseconds();
        pacer.submitFrame(frame);
        pacer.renderOnMainThread();
        renderer.waitForFrame();
    }
    require(stats.renderedFrames == 1 && stats.pacerDroppedFrames == 0,
            "assembled sample decoded and presented once through production components");
}
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    SDL_SetAssertionHandler(reportAssertion, nullptr);
    SDL_LogGetOutputFunction(&savedOutput, &savedOutputContext);
    SDL_LogSetOutputFunction(reportLog, nullptr);
    probe();
    require(errorLogs == 0, "native probe has no SDL error logs");
    if (argc == 1) {
        std::puts("sdl_video_renderer=passed (native probe, no window or GPU)");
        return 0;
    }
    require(argc == 2 && std::strcmp(argv[1], "--render") == 0, "known runtime test option");
    require(SDL_setenv("SDL_VIDEODRIVER", "windows", 1) == 0, "select Windows video");
    require(SDL_Init(SDL_INIT_VIDEO) == 0, "initialize native Windows video");
    SDL_Window* window = SDL_CreateWindow("SDL native video verification", 0, 0, 640, 360, SDL_WINDOW_HIDDEN);
    require(window != nullptr, "create hidden native window");
    for (int videoFormat : {VIDEO_FORMAT_H264, VIDEO_FORMAT_H264_HIGH8_444}) {
        Host host;
        SdlRenderer renderer(&host);
        DECODER_PARAMETERS params{};
        params.window = window;
        params.videoFormat = videoFormat;
        params.width = 640;
        params.height = 360;
        params.frameRate = 60;
        require(renderer.initialize(&params), "initialize production window renderer");
        require(host.flushes == 1, "delegate renderer-recreation event flush once");
        renderer.prepareToRender();
        SDL_Renderer* native = SDL_GetRenderer(window);
        require(native != nullptr, "renderer belongs to supplied window");
        SDL_RendererInfo info{};
        require(SDL_GetRendererInfo(native, &info) == 0, "query actual renderer");
        std::printf("native renderer: %s\n", info.name);
        bool threaded = SDL_strcmp(info.name, "direct3d11") == 0 || SDL_strcmp(info.name, "direct3d12") == 0 ||
                        SDL_strcmp(info.name, "metal") == 0;
        require(renderer.isRenderThreadSupported() == threaded, "preserve render-thread backend selection");
        // A presented window backbuffer may be discarded. Retain the draw output
        // in an SDL target texture so pixel assertions do not depend on swapchain contents.
        SDL_Texture* target = SDL_CreateTexture(native, SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET, 640, 360);
        require(target != nullptr && SDL_SetRenderTarget(native, target) == 0, "select retained readback target");
        const std::array<AVPixelFormat, 4> formats = videoFormat == VIDEO_FORMAT_H264 ?
            std::array<AVPixelFormat, 4>{AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_NV21, AV_PIX_FMT_YUV420P} :
            std::array<AVPixelFormat, 4>{AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P};
        for (size_t index = 0; index < formats.size(); ++index) {
            const bool white = index % 2 == 0;
            AVFrame* frame = grayFrame(formats[index], 640, 360, white ? 235 : 16);
            require(renderer.testRenderFrame(frame), "accept renderer's negotiated pixel layout");
            renderer.renderFrame(frame);
            expectColor(native, 320, 180, white ? 255 : 0, white ? 255 : 0, white ? 255 : 0);
            av_frame_free(&frame);
        }
        AVFrame* frame = grayFrame(formats[0], 640, 360, 235);
        host.enabled[Overlay::OverlayDebug] = true;
        host.surfaces[Overlay::OverlayDebug] = overlay(255, 0, 0);
        renderer.renderFrame(frame);
        require(host.surfaces[Overlay::OverlayDebug] == nullptr, "overlay surface ownership transfers to renderer");
        expectColor(native, 8, 8, 255, 0, 0);
        expectColor(native, 320, 180, 255, 255, 255);
        renderer.renderFrame(frame);
        require(host.queries[Overlay::OverlayDebug] == 2, "query updates while preserving cached texture");
        expectColor(native, 8, 8, 255, 0, 0);
        host.surfaces[Overlay::OverlayDebug] = overlay(0, 0, 255);
        renderer.renderFrame(frame);
        expectColor(native, 8, 8, 0, 0, 255);
        host.enabled[Overlay::OverlayDebug] = false;
        renderer.renderFrame(frame);
        require(host.queries[Overlay::OverlayDebug] == 3, "disabled overlay does not request surfaces");
        expectColor(native, 8, 8, 255, 255, 255);
        host.enabled[Overlay::OverlayDebug] = true;
        host.enabled[Overlay::OverlayStatusUpdate] = true;
        host.surfaces[Overlay::OverlayStatusUpdate] = overlay(0, 255, 0);
        renderer.renderFrame(frame);
        expectColor(native, 8, 8, 0, 0, 255);
        expectColor(native, 8, 352, 0, 255, 0);
        av_frame_free(&frame);
        // New dimensions recreate the upload texture while preserving aspect ratio and overlays.
        frame = grayFrame(formats[0], 180, 180, 235);
        renderer.renderFrame(frame);
        expectColor(native, 320, 180, 255, 255, 255);
        expectColor(native, 100, 180, 0, 0, 0);
        expectColor(native, 8, 8, 0, 0, 255);
        av_frame_free(&frame);
        require(SDL_SetRenderTarget(native, nullptr) == 0, "restore window render target");
        SDL_DestroyTexture(target);
    }
    require(SDL_GetRenderer(window) == nullptr, "renderer destruction releases SDL window resources");
    {
        SdlRenderer renderer(nullptr);
        SDL_Event marker{};
        marker.type = SDL_USEREVENT;
        marker.user.code = 123;
        require(SDL_PushEvent(&marker) == 1, "queue unrelated user event");
        DECODER_PARAMETERS params{};
        params.window = window;
        params.videoFormat = VIDEO_FORMAT_H264;
        require(renderer.initialize(&params), "standalone renderer performs local event flush");
        SDL_Event result{};
        require(SDL_PeepEvents(&result, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT) == 1 &&
                result.user.code == 123, "local window flush preserves unrelated user events");
    }
    using namespace VideoSamples;
    decodeAndPresent(window, AV_CODEC_ID_H264, VIDEO_FORMAT_H264, k_H264TestFrame);
    decodeAndPresent(window, AV_CODEC_ID_HEVC, VIDEO_FORMAT_H265, k_HEVCMainTestFrame);
    decodeAndPresent(window, AV_CODEC_ID_AV1, VIDEO_FORMAT_AV1_MAIN8, k_AV1Main8TestFrame);
    decodeAndPresent(window, AV_CODEC_ID_H264, VIDEO_FORMAT_H264_HIGH8_444, k_h264High_444TestFrame);
    decodeAndPresent(window, AV_CODEC_ID_HEVC, VIDEO_FORMAT_H265_REXT8_444, k_HEVCRExt8_444TestFrame);
    decodeAndPresent(window, AV_CODEC_ID_AV1, VIDEO_FORMAT_AV1_HIGH8_444, k_AV1High8_444TestFrame);
    SDL_DestroyWindow(window);
    SDL_Quit();
    require(errorLogs == 0, "actual renderer and pacer report no SDL errors");
    std::puts("sdl_video_renderer_runtime=passed (hidden Windows window)");
}
