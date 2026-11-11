#pragma once

#include <functional>
#include <memory>

// Construct and destroy on the worker owner thread. Producers can post from
// other threads while the owner pumps Windows messages.
class WindowsWorkerDispatch
{
public:
    explicit WindowsWorkerDispatch(std::function<void()> drainInput);
    ~WindowsWorkerDispatch();

    bool active() const;
    bool postInput() const;
    bool postLifecycle(std::function<void()> completion);
    void finishLifecycle();

private:
    struct Window;
    std::unique_ptr<Window> m_Window;
};
