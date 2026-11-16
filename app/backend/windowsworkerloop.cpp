#include "windowsworkerloop.h"
#include "sessionapirpc.h"

#include <windows.h>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

int WindowsWorkerLoop::run(
    const std::function<void(WorkerCommandRouting::Input, std::string_view)>& handle)
{
    using Input = WorkerCommandRouting::Input;
    const std::unique_ptr<void, decltype(&CloseHandle)> readable(
        CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
    if (!readable) return 1;
    const HANDLE event = readable.get();

    // Establish the owner message queue before the parent sees readiness.
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    if (!WorkerCommandPipe::write(WorkerCommandPipe::Ready)) return 1;

    std::mutex mutex;
    std::deque<std::pair<Input, std::string>> pending;
    std::thread reader([&] {
        WorkerCommandRouting::run(stdin, [&](Input input, std::string line) {
            {
                std::lock_guard lock(mutex);
                pending.emplace_back(input, std::move(line));
            }
            SetEvent(event);
        });
    });

    int result = 0;
    bool running = true;
    while (running) {
        const DWORD wake = MsgWaitForMultipleObjectsEx(1, &event, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wake != WAIT_OBJECT_0 && wake != WAIT_OBJECT_0 + 1) {
            result = 1;
            break;
        }
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                result = static_cast<int>(message.wParam);
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running || wake != WAIT_OBJECT_0) continue;
        std::deque<std::pair<Input, std::string>> batch;
        {
            std::lock_guard lock(mutex);
            batch.swap(pending);
        }
        for (const auto& [input, line] : batch) {
            handle(input, line);
            if (input == Input::Shutdown || input == Input::EndOfInput) {
                running = false;
                break;
            }
        }
    }

    SessionApiRpc::broker().close();
    // On an owner-driven exit stdin may still be open. A cancellation can
    // race with the next read, so keep cancelling until the reader exits.
    const HANDLE readerHandle = static_cast<HANDLE>(reader.native_handle());
    while (WaitForSingleObject(readerHandle, 10) == WAIT_TIMEOUT) CancelSynchronousIo(readerHandle);
    reader.join();
    return result;
}
