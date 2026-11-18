#pragma once

#include <functional>
#include <string>

// Installed before initialization. These run on the producer thread; the
// bridge queues them on its owner before touching protocol or lifetime state.
struct SessionEvents {
    // Stage names and navigation tokens point to static ASCII strings.
    std::function<void(const char*)> stageStarting;
    std::function<void(const char*, int)> stageFailed;
    std::function<void(std::string)> launchFailed;
    std::function<void(std::string)> hostQuitFailed;
    std::function<void()> started;
    std::function<void()> retired;
    std::function<void(bool, bool, bool)> controlsChanged;
    std::function<void(const char*)> controlsNavigate;
    std::function<void(int, int, bool, std::string)> bitrateResult;
};
