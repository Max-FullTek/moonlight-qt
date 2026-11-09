#pragma once

#include "../../decoder.h"
#include "../renderer.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

// The maximum number of frames pacer will ever hold is:
// - 3 frames in the pacing queue
// - 1 frame removed from the render queue in the process of rendering
// - 1 frame for deferred free
#define PACER_MAX_OUTSTANDING_FRAMES (3 + 1 + 1)

class IVsyncSource {
public:
    virtual ~IVsyncSource() {}
    virtual bool initialize(SDL_Window* window, int displayFps) = 0;

    // Asynchronous sources produce callbacks on their own, while synchronous
    // sources require calls to waitForVsync().
    virtual bool isAsync() = 0;

    virtual void waitForVsync() {
        // Synchronous sources must implement waitForVsync()!
        SDL_assert(false);
    }
};

class Pacer
{
public:
    // Optional source factory also permits exercising pacing without a monitor.
    // A null factory selects the existing platform source.
    using VsyncSourceFactory = IVsyncSource* (*)(Pacer*);
    Pacer(IFFmpegRenderer* renderer, PVIDEO_STATS videoStats,
          VsyncSourceFactory vsyncSourceFactory = nullptr);

    ~Pacer();

    void submitFrame(AVFrame* frame);

    bool initialize(SDL_Window* window, int maxVideoFps, bool enablePacing);

    void signalVsync();

    void renderOnMainThread();

private:
    static int vsyncThread(void* context);

    static int renderThread(void* context);

    void handleVsync(int timeUntilNextVsyncMillis);

    void enqueueFrameForRenderingAndUnlock(AVFrame* frame);

    void renderFrame(AVFrame* frame);

    void dropFrameForEnqueue(std::deque<AVFrame*>& queue);

#ifdef HAS_QT_SDL_WAYLAND_BRIDGE
    SDL_Window* m_Window = nullptr;
#endif
    std::deque<AVFrame*> m_RenderQueue;
    std::deque<AVFrame*> m_PacingQueue;
    std::deque<int> m_PacingQueueHistory;
    std::deque<int> m_RenderQueueHistory;
    std::mutex m_FrameQueueLock;
    std::condition_variable_any m_RenderQueueNotEmpty;
    std::condition_variable_any m_PacingQueueNotEmpty;
    std::condition_variable_any m_VsyncSignalled;
    SDL_Thread* m_RenderThread;
    SDL_Thread* m_VsyncThread;
    AVFrame* m_DeferredFreeFrame;
    std::atomic<bool> m_Stopping;

    IVsyncSource* m_VsyncSource;
    VsyncSourceFactory m_VsyncSourceFactory;
    IFFmpegRenderer* m_VsyncRenderer;
    int m_MaxVideoFps;
    int m_DisplayFps;
    PVIDEO_STATS m_VideoStats;
    int m_RendererAttributes;
};
