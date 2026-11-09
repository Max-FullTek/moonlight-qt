#include "pacer.h"
#include "streaming/streamutils.h"

#ifdef HAS_QT_SDL_WAYLAND_BRIDGE
#include "streaming/waylandqtsdlbridge.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "dxvsyncsource.h"
#endif

#ifdef HAS_WAYLAND
#include "waylandvsyncsource.h"
#endif

#include <SDL_syswm.h>
#include <chrono>
#include <utility>

namespace {
template<typename T>
T takeFront(std::deque<T>& queue)
{
    T value = queue.front();
    queue.pop_front();
    return value;
}
}

// Limit the number of queued frames to prevent excessive memory consumption
// if the V-Sync source or renderer is blocked for a while. It's important
// that the sum of all queued frames between both pacing and rendering queues
// must not exceed the number buffer pool size to avoid running the decoder
// out of available decoding surfaces.
#define MAX_QUEUED_FRAMES 3
static_assert(PACER_MAX_OUTSTANDING_FRAMES == MAX_QUEUED_FRAMES + 2,
              "PACER_MAX_OUTSTANDING_FRAMES and MAX_QUEUED_FRAMES must agree");

// We may be woken up slightly late so don't go all the way
// up to the next V-sync since we may accidentally step into
// the next V-sync period. It also takes some amount of time
// to do the render itself, so we can't render right before
// V-sync happens.
#define TIMER_SLACK_MS 3

Pacer::Pacer(IFFmpegRenderer* renderer, PVIDEO_STATS videoStats,
             VsyncSourceFactory vsyncSourceFactory) :
    m_RenderThread(nullptr),
    m_VsyncThread(nullptr),
    m_DeferredFreeFrame(nullptr),
    m_Stopping(false),
    m_VsyncSource(nullptr),
    m_VsyncSourceFactory(vsyncSourceFactory),
    m_VsyncRenderer(renderer),
    m_MaxVideoFps(0),
    m_DisplayFps(0),
    m_VideoStats(videoStats)
{

}

Pacer::~Pacer()
{
    {
        // Serialize the stop transition with condition-variable waiters.
        std::lock_guard<std::mutex> lock(m_FrameQueueLock);
        m_Stopping = true;
    }
    m_PacingQueueNotEmpty.notify_all();
    m_RenderQueueNotEmpty.notify_all();
    m_VsyncSignalled.notify_all();

    // Stop the V-sync thread
    if (m_VsyncThread != nullptr) {
        SDL_WaitThread(m_VsyncThread, nullptr);
    }

    // Stop V-sync callbacks
    delete m_VsyncSource;
    m_VsyncSource = nullptr;

    // Stop the render thread
    if (m_RenderThread != nullptr) {
        SDL_WaitThread(m_RenderThread, nullptr);
    }
    else {
        // Notify the renderer that it is being destroyed soon
        // NB: This must happen on the same thread that calls renderFrame().
        m_VsyncRenderer->cleanupRenderContext();
    }

    // Delete any remaining unconsumed frames
    while (!m_RenderQueue.empty()) {
        AVFrame* frame = takeFront(m_RenderQueue);
        av_frame_free(&frame);
    }
    while (!m_PacingQueue.empty()) {
        AVFrame* frame = takeFront(m_PacingQueue);
        av_frame_free(&frame);
    }
    av_frame_free(&m_DeferredFreeFrame);
}

void Pacer::renderOnMainThread()
{
    // Ignore this call for renderers that work on a dedicated render thread
    if (m_RenderThread != nullptr) {
        return;
    }

    m_FrameQueueLock.lock();

    if (!m_RenderQueue.empty()) {
        AVFrame* frame = takeFront(m_RenderQueue);
        m_FrameQueueLock.unlock();

        renderFrame(frame);
    }
    else {
        m_FrameQueueLock.unlock();
    }
}

int Pacer::vsyncThread(void *context)
{
    Pacer* me = reinterpret_cast<Pacer*>(context);

#if SDL_VERSION_ATLEAST(2, 0, 9)
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
#else
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
#endif

    bool async = me->m_VsyncSource->isAsync();
    while (!me->m_Stopping) {
        if (async) {
            // Wait for the VSync source to invoke signalVsync() or 100ms to elapse
            me->m_FrameQueueLock.lock();
            if (!me->m_Stopping) {
                me->m_VsyncSignalled.wait_for(me->m_FrameQueueLock, std::chrono::milliseconds(100));
            }
            me->m_FrameQueueLock.unlock();
        }
        else {
            // Let the VSync source wait in the context of our thread
            me->m_VsyncSource->waitForVsync();
        }

        if (me->m_Stopping) {
            break;
        }

        me->handleVsync(1000 / me->m_DisplayFps);
    }

    return 0;
}

int Pacer::renderThread(void* context)
{
    Pacer* me = reinterpret_cast<Pacer*>(context);

    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to set render thread to high priority: %s",
                    SDL_GetError());
    }

    while (!me->m_Stopping) {
        // Wait for the renderer to be ready for the next frame
        me->m_VsyncRenderer->waitToRender();

        // Acquire the frame queue lock to protect the queue and
        // the not empty condition
        me->m_FrameQueueLock.lock();

        // Wait for a frame to be ready to render
        while (!me->m_Stopping && me->m_RenderQueue.empty()) {
            me->m_RenderQueueNotEmpty.wait(me->m_FrameQueueLock);
        }

        if (me->m_Stopping) {
            // Exit this thread
            me->m_FrameQueueLock.unlock();
            break;
        }

        AVFrame* frame = takeFront(me->m_RenderQueue);
        me->m_FrameQueueLock.unlock();

        me->renderFrame(frame);
    }

    // Notify the renderer that it is being destroyed soon
    // NB: This must happen on the same thread that calls renderFrame().
    me->m_VsyncRenderer->cleanupRenderContext();

    return 0;
}

void Pacer::enqueueFrameForRenderingAndUnlock(AVFrame *frame)
{
    dropFrameForEnqueue(m_RenderQueue);
    m_RenderQueue.push_back(frame);

    m_FrameQueueLock.unlock();

    if (m_RenderThread != nullptr) {
        m_RenderQueueNotEmpty.notify_one();
    }
    else {
        SDL_Event event;

        // For main thread rendering, we'll push an event to trigger a callback
        event.type = SDL_USEREVENT;
        event.user.code = SDL_CODE_FRAME_READY;
        SDL_PushEvent(&event);
    }
}

// Called in an arbitrary thread by the IVsyncSource on V-sync
// or an event synchronized with V-sync
void Pacer::handleVsync(int timeUntilNextVsyncMillis)
{
    // Make sure initialize() has been called
    SDL_assert(m_MaxVideoFps != 0);

    m_FrameQueueLock.lock();

    // If the queue length history entries are large, be strict
    // about dropping excess frames.
    int frameDropTarget = 1;

    // If we may get more frames per second than we can display, use
    // frame history to drop frames only if consistently above the
    // one queued frame mark.
    if (m_MaxVideoFps >= m_DisplayFps) {
        for (int queueHistoryEntry : std::as_const(m_PacingQueueHistory)) {
            if (queueHistoryEntry <= 1) {
                // Be lenient as long as the queue length
                // resolves before the end of frame history
                frameDropTarget = 3;
                break;
            }
        }

        // Keep a rolling 500 ms window of pacing queue history
        if (static_cast<int>(m_PacingQueueHistory.size()) == m_DisplayFps / 2) {
            takeFront(m_PacingQueueHistory);
        }

        m_PacingQueueHistory.push_back(static_cast<int>(m_PacingQueue.size()));
    }

    // Catch up if we're several frames ahead
    while (static_cast<int>(m_PacingQueue.size()) > frameDropTarget) {
        AVFrame* frame = takeFront(m_PacingQueue);

        // Drop the lock while we call av_frame_free()
        m_FrameQueueLock.unlock();
        m_VideoStats->pacerDroppedFrames++;
        av_frame_free(&frame);
        m_FrameQueueLock.lock();
    }

    if (m_PacingQueue.empty()) {
        // A notification can be spurious; never dequeue an empty frame queue.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(
            SDL_max(timeUntilNextVsyncMillis, TIMER_SLACK_MS) - TIMER_SLACK_MS);
        while (!m_Stopping && m_PacingQueue.empty()) {
            if (m_PacingQueueNotEmpty.wait_until(m_FrameQueueLock, deadline) == std::cv_status::timeout) {
                break;
            }
        }
    }
    if (m_Stopping || m_PacingQueue.empty()) {
        m_FrameQueueLock.unlock();
        return;
    }

    // Place the first frame on the render queue
    enqueueFrameForRenderingAndUnlock(takeFront(m_PacingQueue));
}

bool Pacer::initialize(SDL_Window* window, int maxVideoFps, bool enablePacing)
{
#ifdef HAS_QT_SDL_WAYLAND_BRIDGE
    m_Window = window;
#endif
    m_MaxVideoFps = maxVideoFps;
    m_DisplayFps = StreamUtils::getDisplayRefreshRate(window);
    m_RendererAttributes = m_VsyncRenderer->getRendererAttributes();

    if (enablePacing) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Frame pacing: target %d Hz with %d FPS stream",
                    m_DisplayFps, m_MaxVideoFps);

        if (m_VsyncSourceFactory != nullptr) {
            m_VsyncSource = m_VsyncSourceFactory(this);
        }
        else {
            SDL_SysWMinfo info;
            SDL_VERSION(&info.version);
            if (!SDL_GetWindowWMInfo(window, &info)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "SDL_GetWindowWMInfo() failed: %s",
                             SDL_GetError());
                return false;
            }

            switch (info.subsystem) {
        #ifdef _WIN32
            case SDL_SYSWM_WINDOWS:
                m_VsyncSource = new DxVsyncSource(this);
                break;
        #endif

        #if defined(SDL_VIDEO_DRIVER_WAYLAND) && defined(HAS_WAYLAND)
            case SDL_SYSWM_WAYLAND:
                m_VsyncSource = new WaylandVsyncSource(this);
                break;
        #endif

            default:
                // Platforms without a VsyncSource will just render frames
                // immediately like they used to.
                break;
            }
        }

        SDL_assert(m_VsyncSource != nullptr || !(m_RendererAttributes & RENDERER_ATTRIBUTE_FORCE_PACING));

        if (m_VsyncSource != nullptr && !m_VsyncSource->initialize(window, m_DisplayFps)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Vsync source failed to initialize. Frame pacing will not be available!");
            delete m_VsyncSource;
            m_VsyncSource = nullptr;
        }
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Frame pacing disabled: target %d Hz with %d FPS stream",
                    m_DisplayFps, m_MaxVideoFps);
    }

    if (m_VsyncSource != nullptr) {
        m_VsyncThread = SDL_CreateThread(Pacer::vsyncThread, "PacerVsync", this);
    }

    if (m_VsyncRenderer->isRenderThreadSupported()) {
        m_RenderThread = SDL_CreateThread(Pacer::renderThread, "PacerRender", this);
    }

    return true;
}

void Pacer::signalVsync()
{
    m_VsyncSignalled.notify_one();
}

void Pacer::renderFrame(AVFrame* frame)
{
    // Count time spent in Pacer's queues
    uint64_t beforeRender = LiGetMicroseconds();
    m_VideoStats->totalPacerTimeUs += (beforeRender - (uint64_t)frame->pkt_dts);

    // Render it
    {
#ifdef HAS_QT_SDL_WAYLAND_BRIDGE
        auto surfaceLock = WaylandQtSdlBridge::lockSurfaceForRendering(m_Window);
#endif
        m_VsyncRenderer->renderFrame(frame);
    }
    uint64_t afterRender = LiGetMicroseconds();

    m_VideoStats->totalRenderTimeUs += (afterRender - beforeRender);
    m_VideoStats->renderedFrames++;

    // Wait until after next frame to free this one to ensure the GPU
    // doesn't stall or read garbage if the backing buffer gets returned
    // to the pool and the decoder tries to write a new frame into it
    std::swap(frame, m_DeferredFreeFrame);
    av_frame_free(&frame);

    // Drop frames if we have too many queued up for a while
    m_FrameQueueLock.lock();

    int frameDropTarget;

    if (m_RendererAttributes & RENDERER_ATTRIBUTE_NO_BUFFERING) {
        // Renderers that don't buffer any frames but don't support waitToRender() need us to buffer
        // an extra frame to ensure they don't starve while waiting to present.
        frameDropTarget = 1;
    }
    else {
        frameDropTarget = 0;
        for (int queueHistoryEntry : std::as_const(m_RenderQueueHistory)) {
            if (queueHistoryEntry == 0) {
                // Be lenient as long as the queue length
                // resolves before the end of frame history
                frameDropTarget = 2;
                break;
            }
        }

        // Keep a rolling 500 ms window of render queue history
        if (static_cast<int>(m_RenderQueueHistory.size()) == m_MaxVideoFps / 2) {
            takeFront(m_RenderQueueHistory);
        }

        m_RenderQueueHistory.push_back(static_cast<int>(m_RenderQueue.size()));
    }

    // Catch up if we're several frames ahead
    while (static_cast<int>(m_RenderQueue.size()) > frameDropTarget) {
        AVFrame* frame = takeFront(m_RenderQueue);

        // Drop the lock while we call av_frame_free()
        m_FrameQueueLock.unlock();
        m_VideoStats->pacerDroppedFrames++;
        av_frame_free(&frame);
        m_FrameQueueLock.lock();
    }

    m_FrameQueueLock.unlock();
}

void Pacer::dropFrameForEnqueue(std::deque<AVFrame*>& queue)
{
    SDL_assert(queue.size() <= MAX_QUEUED_FRAMES);
    if (queue.size() == MAX_QUEUED_FRAMES) {
        AVFrame* frame = takeFront(queue);
        av_frame_free(&frame);
    }
}

void Pacer::submitFrame(AVFrame* frame)
{
    // Make sure initialize() has been called
    SDL_assert(m_MaxVideoFps != 0);

    // Queue the frame and possibly wake up the render thread
    m_FrameQueueLock.lock();
    if (m_VsyncSource != nullptr) {
        dropFrameForEnqueue(m_PacingQueue);
        m_PacingQueue.push_back(frame);
        m_FrameQueueLock.unlock();
        m_PacingQueueNotEmpty.notify_one();
    }
    else {
        enqueueFrameForRenderingAndUnlock(frame);
    }
}
