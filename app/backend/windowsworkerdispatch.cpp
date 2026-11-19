#include "windowsworkerdispatch.h"

#include <windows.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <exception>
#include <mutex>
#include <utility>

struct WindowsWorkerDispatch::Window {
    static constexpr wchar_t ClassName[] = L"MoonlightLiteCoreInputWake";
    static constexpr UINT WakeMessage = WM_APP;
    static constexpr UINT LifecycleMessage = WM_APP + 1;
    static constexpr UINT CompletionMessage = WM_APP + 2;
    static constexpr UINT RetryPostMessage = WM_APP + 3;
    static constexpr UINT_PTR RetryTimer = 1;
    static constexpr std::array<UINT, 2> RetriableMessages{WakeMessage, CompletionMessage};
    std::array<size_t, RetriableMessages.size()> pendingPosts{};
    bool retryTimerActive = false;
    std::mutex completionMutex;
    std::deque<std::function<void()>> completions;
    std::mutex lifecycleMutex;
    std::function<void()> lifecycleCompletion;
    HWND window = nullptr;

    explicit Window(std::function<void()> drainInput)
        : drainInput(std::move(drainInput))
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = handleMessage;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = ClassName;
        if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        window = CreateWindowExW(0, ClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                 nullptr, instance, this);
    }

    ~Window()
    {
        if (window != nullptr) {
            DestroyWindow(window);
        }
    }

    bool post(UINT message) const
    {
        if (!window) return false;
        if (PostMessageW(window, message, 0, 0)) return true;
        // A sent notification can reach the owner when its posted queue is
        // full. It only arms a timer, even when called on the owner thread.
        return GetLastError() == ERROR_NOT_ENOUGH_QUOTA &&
                SendNotifyMessageW(window, RetryPostMessage, message, 0) != 0;
    }

    void scheduleRetry(UINT message)
    {
        for (size_t i = 0; i < RetriableMessages.size(); ++i) {
            if (message != RetriableMessages[i]) continue;
            ++pendingPosts[i];
            if (!retryTimerActive) {
                if (!SetTimer(window, RetryTimer, USER_TIMER_MINIMUM, nullptr)) {
                    std::fputs("Unable to schedule a Windows worker wake retry\n", stderr);
                    std::terminate();
                }
                retryTimerActive = true;
            }
            return;
        }
    }

    void retryPendingPosts()
    {
        for (size_t i = 0; i < RetriableMessages.size(); ++i) {
            while (pendingPosts[i] > 0) {
                if (!PostMessageW(window, RetriableMessages[i], 0, 0)) {
                    if (GetLastError() == ERROR_NOT_ENOUGH_QUOTA) return;
                    std::fputs("Unable to retry a Windows worker wake\n", stderr);
                    std::terminate();
                }
                --pendingPosts[i];
            }
        }
        KillTimer(window, RetryTimer);
        retryTimerActive = false;
    }

    // The launch completion consumes its slot before entering the stream loop.
    // Cleanup can then publish the next completion using the same slot.
    bool postLifecycle(std::function<void()> completion)
    {
        {
            std::lock_guard lock(lifecycleMutex);
            assert(!lifecycleCompletion);
            lifecycleCompletion = std::move(completion);
        }
        return window && PostMessageW(window, LifecycleMessage, 0, 0) != 0;
    }

    void finishLifecycle()
    {
        std::function<void()> completion;
        {
            std::lock_guard lock(lifecycleMutex);
            completion.swap(lifecycleCompletion);
        }
        if (completion) completion();
    }

    bool postCompletion(std::function<void()>&& completion)
    {
        std::lock_guard lock(completionMutex);
        completions.push_back(std::move(completion));
        if (post(CompletionMessage)) return true;
        completion = std::move(completions.back());
        completions.pop_back();
        return false;
    }

    void finishCompletion()
    {
        std::function<void()> completion;
        {
            std::lock_guard lock(completionMutex);
            if (completions.empty()) return;
            completion = std::move(completions.front());
            completions.pop_front();
        }
        completion();
    }

    const std::function<void()> drainInput;

    static LRESULT CALLBACK handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        else if (message == WakeMessage || message == LifecycleMessage || message == CompletionMessage ||
                 message == RetryPostMessage || (message == WM_TIMER && wParam == RetryTimer)) {
            auto* dispatcher = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (dispatcher != nullptr) {
                if (message == WakeMessage) dispatcher->drainInput();
                else if (message == LifecycleMessage) dispatcher->finishLifecycle();
                else if (message == CompletionMessage) dispatcher->finishCompletion();
                else if (message == RetryPostMessage) dispatcher->scheduleRetry(static_cast<UINT>(wParam));
                else dispatcher->retryPendingPosts();
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

WindowsWorkerDispatch::WindowsWorkerDispatch(std::function<void()> drainInput)
    : m_Window(std::make_unique<Window>(std::move(drainInput)))
{
}

WindowsWorkerDispatch::~WindowsWorkerDispatch() = default;

bool WindowsWorkerDispatch::active() const
{
    return m_Window->window != nullptr;
}

bool WindowsWorkerDispatch::postInput() const
{
    return m_Window->post(Window::WakeMessage);
}

bool WindowsWorkerDispatch::postLifecycle(std::function<void()> completion)
{
    return m_Window->postLifecycle(std::move(completion));
}

void WindowsWorkerDispatch::finishLifecycle()
{
    m_Window->finishLifecycle();
}

bool WindowsWorkerDispatch::postCompletion(std::function<void()>&& completion)
{
    return m_Window->postCompletion(std::move(completion));
}

int WindowsWorkerDispatch::runOwnerLoop()
{
    MSG message{};
    for (;;) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) return 1;
        if (result == 0) return static_cast<int>(message.wParam);
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void WindowsWorkerDispatch::exitOwnerLoop(int result)
{
    PostQuitMessage(result);
}
