#include "backend/windowsworkerloop.h"

#include <windows.h>

int main(int argc, char** argv)
{
    using namespace WorkerCommandPipe;
    if (argc != 2 || std::string_view(argv[1]) != "--gpui-core") return 1;
    const DWORD owner = GetCurrentThreadId();
    bool outputOk = true;
    const int result = WindowsWorkerLoop::run([&](WorkerCommandRouting::Input status, std::string_view line) {
        if (GetCurrentThreadId() != owner) {
            outputOk = false;
            return;
        }
        if (status == WorkerCommandRouting::Input::Oversized) {
            outputOk = write(R"({"event":"error","protocol":1,"code":"request_too_large"})");
        } else if (status == WorkerCommandRouting::Input::InvalidJson) {
            outputOk = write(R"({"event":"error","protocol":1,"code":"invalid_json"})");
        } else if (line.size() > MaxControlLineBytes && line.find("monitor_update") != std::string_view::npos) {
            outputOk = write(R"({"event":"test_monitor_received","protocol":1})");
        } else if (line == R"({"command":"test_owner_exit"})") {
            PostQuitMessage(0);
        }
    });
    return outputOk ? result : 3;
}
