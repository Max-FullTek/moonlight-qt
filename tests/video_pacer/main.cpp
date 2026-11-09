#include "streaming/video/ffmpeg-renderers/pacer/pacer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {
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

struct Frames {
    std::atomic<int> live{0};
    static void freeBuffer(void* context, uint8_t* bytes)
    {
        --static_cast<Frames*>(context)->live;
        av_free(bytes);
    }
    AVFrame* make(int id)
    {
        AVFrame* frame = av_frame_alloc();
        require(frame != nullptr, "allocate frame");
        auto* data = static_cast<uint8_t*>(av_malloc(1));
        require(data != nullptr, "allocate tracked frame buffer");
        frame->buf[0] = av_buffer_create(data, 1, freeBuffer, this, 0);
        require(frame->buf[0] != nullptr, "retain tracked frame buffer");
        ++live;
        frame->pts = id;
        frame->pkt_dts = LiGetMicroseconds();
        return frame;
    }
};

class Renderer : public IFFmpegRenderer {
public:
    explicit Renderer(bool threaded = false, int attributes = 0) :
        IFFmpegRenderer(RendererType::Unknown), threaded(threaded), attributes(attributes) {}
    bool initialize(PDECODER_PARAMETERS) override { return true; }
    bool prepareDecoderContext(AVCodecContext*, AVDictionary**) override { return true; }
    bool isRenderThreadSupported() override { return threaded; }
    int getRendererAttributes() override { return attributes; }
    void renderFrame(AVFrame* frame) override
    {
        std::lock_guard<std::mutex> guard(lock);
        renderThread = std::this_thread::get_id();
        rendered.push_back(static_cast<int>(frame->pts));
        changed.notify_all();
    }
    void cleanupRenderContext() override
    {
        std::lock_guard<std::mutex> guard(lock);
        cleanupThread = std::this_thread::get_id();
        ++cleanups;
    }
    bool waitForCount(size_t count)
    {
        std::unique_lock<std::mutex> guard(lock);
        return changed.wait_for(guard, std::chrono::seconds(2), [&] { return rendered.size() >= count; });
    }
    bool threaded;
    int attributes;
    std::mutex lock;
    std::condition_variable changed;
    std::vector<int> rendered;
    int cleanups = 0;
    std::thread::id renderThread, cleanupThread;
};

class AsyncVsync : public IVsyncSource {
public:
    bool initialize(SDL_Window*, int) override { return true; }
    bool isAsync() override { return true; }
};
IVsyncSource* asyncSource(Pacer*) { return new AsyncVsync; }

class FailedVsync : public AsyncVsync {
public:
    bool initialize(SDL_Window*, int) override { return false; }
};
IVsyncSource* failedSource(Pacer*) { return new FailedVsync; }

struct VsyncGate {
    std::mutex lock;
    std::condition_variable changed;
    int ticks = 0;
    bool closing = false;
    void tick()
    {
        std::lock_guard<std::mutex> guard(lock);
        ++ticks;
        changed.notify_one();
    }
    void close()
    {
        std::lock_guard<std::mutex> guard(lock);
        closing = true;
        changed.notify_all();
    }
};
VsyncGate* currentGate = nullptr;
class GatedVsync : public IVsyncSource {
public:
    explicit GatedVsync(VsyncGate* gate) : gate(gate) {}
    bool initialize(SDL_Window*, int) override { return true; }
    bool isAsync() override { return false; }
    void waitForVsync() override
    {
        std::unique_lock<std::mutex> guard(gate->lock);
        gate->changed.wait(guard, [&] { return gate->closing || gate->ticks != 0; });
        if (gate->ticks) --gate->ticks;
    }
    VsyncGate* gate;
};
IVsyncSource* gatedSource(Pacer*) { return new GatedVsync(currentGate); }

void waitFrameEvent()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        SDL_Event event{};
        if (SDL_WaitEventTimeout(&event, 20) && event.type == SDL_USEREVENT &&
            event.user.code == SDL_CODE_FRAME_READY) {
            return;
        }
    }
    require(false, "pacing delivers main-thread render event");
}

void mainThreadQueues(SDL_Window* window, int attributes)
{
    Renderer renderer(false, attributes);
    Frames frames;
    VIDEO_STATS stats{};
    {
        Pacer pacer(&renderer, &stats);
        require(pacer.initialize(window, 60, false), "initialize unpaced main-thread renderer");
        for (int id = 1; id <= 5; ++id) pacer.submitFrame(frames.make(id));
        require(frames.live == 3, "queue cap discards oldest frames at three");
        pacer.renderOnMainThread();
        require(renderer.rendered == std::vector<int>{3}, "oldest retained frame renders first");
        require(stats.pacerDroppedFrames == (attributes ? 1u : 2u), "preserve render catch-up policy");
        if (attributes) {
            pacer.renderOnMainThread();
            require(renderer.rendered == std::vector<int>({3, 5}), "no-buffering renderer retains one queued frame");
        }
        else {
            pacer.submitFrame(frames.make(6));
            pacer.renderOnMainThread();
            require(renderer.rendered == std::vector<int>({3, 6}), "subsequent frame replaces deferred frame");
        }
        require(frames.live == 1, "latest rendered frame remains retained for GPU lifetime");
        pacer.renderOnMainThread();
        require(stats.renderedFrames == 2, "empty main-thread render is harmless");
    }
    require(frames.live == 0, "destructor releases deferred and queued buffers");
    require(renderer.cleanups == 1 && renderer.cleanupThread == renderer.renderThread &&
            renderer.cleanupThread == std::this_thread::get_id(), "main-thread context cleanup affinity");
}
}

int main()
{
    SDL_SetMainReady();
    SDL_SetAssertionHandler(reportAssertion, nullptr);
    require(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0, "select dummy video");
    require(SDL_Init(SDL_INIT_VIDEO) == 0, "initialize dummy video");
    SDL_Window* window = SDL_CreateWindow("pacer test", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    require(window != nullptr, "create hidden dummy window");
    mainThreadQueues(window, 0);
    mainThreadQueues(window, RENDERER_ATTRIBUTE_NO_BUFFERING);

    for (bool lenient : {false, true}) {
        SDL_FlushEvent(SDL_USEREVENT);
        VsyncGate gate;
        currentGate = &gate;
        Renderer renderer;
        VIDEO_STATS stats{};
        Frames frames;
        {
            Pacer pacer(&renderer, &stats, gatedSource);
            require(pacer.initialize(window, 60, true), "initialize controlled synchronous pacing");
            if (lenient) {
                pacer.submitFrame(frames.make(0));
                gate.tick();
                waitFrameEvent();
                pacer.renderOnMainThread();
            }
            for (int id = 1; id <= 5; ++id) pacer.submitFrame(frames.make(id));
            require(frames.live == (lenient ? 4 : 3), "pacing queue cap plus deferred frame lifetime");
            gate.tick();
            waitFrameEvent();
            pacer.renderOnMainThread();
            require(renderer.rendered.back() == (lenient ? 3 : 5), "pacing history preserves lenient versus strict catch-up");
            require(stats.pacerDroppedFrames == (lenient ? 0u : 2u), "pacing catch-up drop accounting");
            gate.close();
        }
        require(frames.live == 0, "shutdown frees pending paced and deferred frames");
    }

    // Exercise actual SDL render-thread wakeup, stop and buffer release repeatedly.
    for (int iteration = 0; iteration < 40; ++iteration) {
        Renderer renderer(true);
        VIDEO_STATS stats{};
        Frames frames;
        {
            Pacer pacer(&renderer, &stats);
            require(pacer.initialize(window, 60, false), "initialize dedicated render thread");
            for (int id = 0; id < 4; ++id) {
                pacer.submitFrame(frames.make(id));
                require(renderer.waitForCount(id + 1), "render thread wakes for each submitted frame");
            }
        }
        require(stats.renderedFrames == 4 && frames.live == 0, "threaded shutdown releases all frames");
        require(renderer.cleanups == 1 && renderer.cleanupThread == renderer.renderThread &&
                renderer.cleanupThread != std::this_thread::get_id(), "dedicated render context cleanup affinity");
    }

    for (int iteration = 0; iteration < 30; ++iteration) {
        SDL_FlushEvent(SDL_USEREVENT);
        Renderer renderer;
        VIDEO_STATS stats{};
        Frames frames;
        {
            Pacer pacer(&renderer, &stats, asyncSource);
            require(pacer.initialize(window, 60, true), "initialize asynchronous pacing");
            // Empty V-sync notifications must never dequeue an absent frame.
            pacer.signalVsync();
            pacer.submitFrame(frames.make(17));
            pacer.signalVsync();
            waitFrameEvent();
            pacer.renderOnMainThread();
            require(renderer.rendered == std::vector<int>{17}, "paced frame reaches renderer");
            pacer.signalVsync();
        }
        require(frames.live == 0 && renderer.cleanups == 1, "paced shutdown wakes waiters and frees frames");
    }
    {
        Renderer renderer;
        Frames frames;
        VIDEO_STATS stats{};
        {
            Pacer pacer(&renderer, &stats, failedSource);
            require(pacer.initialize(window, 60, true), "failed V-sync initialization keeps unpaced fallback");
            pacer.submitFrame(frames.make(42));
            pacer.renderOnMainThread();
            require(renderer.rendered == std::vector<int>{42}, "fallback renders without V-sync");
        }
        require(frames.live == 0, "fallback releases retained frame");
    }
    // Destruction without initialization and with untouched queues remains valid.
    {
        Renderer renderer;
        VIDEO_STATS stats{};
        { Pacer pacer(&renderer, &stats); }
        require(renderer.cleanups == 1, "uninitialized pacer cleanup");
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("video_pacer=passed (actual AVFrame ownership, dummy video, no physical V-sync)");
}
