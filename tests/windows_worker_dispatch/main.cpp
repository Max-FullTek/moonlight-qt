#include "backend/windowsworkerdispatch.h"

#include <windows.h>
#include <thread>
#include <vector>

int main()
{
    const auto owner = GetCurrentThreadId();
    std::vector<int> events;
    const auto record = [&](int event) {
        events.push_back(GetCurrentThreadId() == owner ? event : -event);
    };
    WindowsWorkerDispatch dispatch([&] { record(1); });
    if (!dispatch.active()) return 1;

    bool posted = false;
    bool postedCleanup = false;
    std::thread producer([&] {
        posted = dispatch.postInput() && dispatch.postLifecycle([&] {
            record(2);
            postedCleanup = dispatch.postLifecycle([&] { record(3); });
        });
    });
    producer.join();
    if (!posted || !events.empty()) return 2;

    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    return postedCleanup && events == std::vector<int>{1, 2, 3} ? 0 : 3;
}
