#include "backend/windowsworkerdispatch.h"
#include "backend/windowstaskqueue.h"

#include <windows.h>
#include <future>
#include <thread>
#include <vector>

int main()
{
    const auto owner = GetCurrentThreadId();
    std::vector<int> jobs;
    {
        WindowsTaskQueue queue;
        std::promise<void> release;
        const auto ready = release.get_future();
        queue.submit([&] {
            ready.wait();
            jobs.push_back(GetCurrentThreadId() == owner ? -1 : 1);
        });
        queue.submit([&] { jobs.push_back(2); });
        queue.submit([&] { jobs.push_back(3); });
        release.set_value();
        queue.wait();
        if (jobs != std::vector<int>{1, 2, 3}) return 8;
        queue.submit([&] { jobs.push_back(4); });
    }
    if (jobs != std::vector<int>{1, 2, 3, 4}) return 9;

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
    if (!postedCleanup || !deferred || events != expected) return 5;

    // The production GPUI entry uses this pump, without QApplication::exec().
    events.clear();
    std::thread exitProducer([&] {
        posted = dispatch.postCompletion([&] {
            record(200);
            WindowsWorkerDispatch::exitOwnerLoop(7);
        });
    });
    exitProducer.join();
    if (!posted) return 6;
    return WindowsWorkerDispatch::runOwnerLoop() == 7 && events == std::vector<int>{200} ? 0 : 7;
}
