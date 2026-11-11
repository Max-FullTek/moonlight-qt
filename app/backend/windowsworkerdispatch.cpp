#include "windowsworkerdispatch.h"

#include <windows.h>
#include <cassert>
#include <mutex>
#include <utility>

struct WindowsWorkerDispatch::Window {
    static constexpr wchar_t ClassName[] = L"MoonlightLiteCoreInputWake";
    static constexpr UINT WakeMessage = WM_APP;
    static constexpr UINT LifecycleMessage = WM_APP + 1;
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

    const std::function<void()> drainInput;

    static LRESULT CALLBACK handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        else if (message == WakeMessage || message == LifecycleMessage) {
            auto* dispatcher = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (dispatcher != nullptr) {
                if (message == WakeMessage) dispatcher->drainInput();
                else dispatcher->finishLifecycle();
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
    return active() && PostMessageW(m_Window->window, Window::WakeMessage, 0, 0) != 0;
}

bool WindowsWorkerDispatch::postLifecycle(std::function<void()> completion)
{
    return m_Window->postLifecycle(std::move(completion));
}

void WindowsWorkerDispatch::finishLifecycle()
{
    m_Window->finishLifecycle();
}
