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
    if (!postedCleanup || events != std::vector<int>{1, 2, 3}) return 3;

    // Multiple host/session completions must coexist, retain FIFO ordering,
    // and retire objects only after the callback that scheduled retirement.
    events.clear();
    bool callbackActive = false;
    bool deferred = false;
    std::thread operations([&] {
        for (int i = 0; i < 64; ++i) {
            if (!dispatch.postCompletion([&, i] { record(i + 10); })) posted = false;
        }
        if (!dispatch.postCompletion([&] {
            callbackActive = true;
            postedCleanup = dispatch.postCompletion([&] {
                deferred = !callbackActive;
                record(100);
            });
            callbackActive = false;
        })) posted = false;
    });
    operations.join();
    if (!posted || !events.empty()) return 4;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    std::vector<int> expected;
    for (int i = 0; i < 64; ++i) expected.push_back(i + 10);
    expected.push_back(100);
    return postedCleanup && deferred && events == expected ? 0 : 5;
}
