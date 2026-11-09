#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

struct SDL_Window;

// Windows-only composition layer on the owner's existing SDL HWND. All calls
// must run on the creating thread; the window must outlive this object.
// No HWND, input handler, timer, thread, or video-frame buffer is created here.
class NativeNotification {
public:
    explicit NativeNotification(SDL_Window* window);
    ~NativeNotification();
    NativeNotification(const NativeNotification&) = delete;
    NativeNotification& operator=(const NativeNotification&) = delete;

    // Returns a Windows HRESULT (negative on failure). Owner decides how to
    // report failures; this module never opens a dialog or steals focus.
    long show(std::wstring_view message, int durationMs);
    long hide();
    long refresh(); // Resize/DPI change, only redraws when visible.
    long advance(); // Owner calls at nextEventDelayMs(), no polling required.
    int nextEventDelayMs() const;
    bool visible() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
