#pragma once

#include <functional>
#include <memory>

// Serial FIFO on the Windows thread pool. Stop producers before waiting or
// destroying the queue. Jobs must not wait on the queue they run on.
class WindowsTaskQueue
{
public:
    WindowsTaskQueue();
    ~WindowsTaskQueue();

    void submit(std::function<void()> job);
    void wait();

private:
    struct State;
    std::unique_ptr<State> m_State;
};
