#include "windowstaskqueue.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <deque>
#include <mutex>
#include <system_error>

struct WindowsTaskQueue::State
{
    std::mutex mutex;
    std::deque<std::function<void()>> jobs;
    bool scheduled = false;
    PTP_WORK work;

    State() : work(CreateThreadpoolWork(run, this, nullptr))
    {
        if (!work) throw std::system_error(GetLastError(), std::system_category(), "CreateThreadpoolWork");
    }

    ~State()
    {
        WaitForThreadpoolWorkCallbacks(work, FALSE);
        CloseThreadpoolWork(work);
    }

    static void CALLBACK run(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept
    {
        auto& state = *static_cast<State*>(context);
        for (;;) {
            std::function<void()> job;
            {
                std::lock_guard lock(state.mutex);
                if (state.jobs.empty()) {
                    state.scheduled = false;
                    return;
                }
                job = std::move(state.jobs.front());
                state.jobs.pop_front();
            }
            job();
        }
    }
};

WindowsTaskQueue::WindowsTaskQueue() : m_State(std::make_unique<State>()) {}
WindowsTaskQueue::~WindowsTaskQueue() = default;

void WindowsTaskQueue::submit(std::function<void()> job)
{
    std::lock_guard lock(m_State->mutex);
    m_State->jobs.push_back(std::move(job));
    if (!m_State->scheduled) {
        m_State->scheduled = true;
        SubmitThreadpoolWork(m_State->work);
    }
}

void WindowsTaskQueue::wait()
{
    WaitForThreadpoolWorkCallbacks(m_State->work, FALSE);
}
