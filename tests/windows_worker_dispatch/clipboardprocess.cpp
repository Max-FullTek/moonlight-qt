#include "streaming/windowsclipboardprocess.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <cstdlib>
#include <string>

static bool check(bool value, const char* message)
{
    if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

int main()
{
    if (const auto mode = std::getenv("MOONLIGHT_CLIPBOARD_PROCESS_TEST_CHILD")) {
        if (std::string(mode) == "hung") { Sleep(30000); return 0; }
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        char bytes[4096];
        size_t count;
        while ((count = std::fread(bytes, 1, sizeof(bytes), stdin)) != 0) {
            std::fwrite(bytes, 1, count, stdout);
            std::fflush(stdout);
        }
        std::fputs("helper stderr\n", stderr);
        return 17;
    }
    wchar_t path[32768];
    const auto pathLength = GetModuleFileNameW(nullptr, path, 32768);
    if (!check(pathLength > 0 && pathLength < 32768, "read executable path")) return 1;
    _putenv_s("MOONLIGHT_CLIPBOARD_PROCESS_TEST_CHILD", "echo");
    {
        WindowsClipboardProcess child;
        if (!check(child.start({path, pathLength}), "start helper without Qt") ||
            !check(child.running(), "helper is running")) return 1;
        std::string payload(2 * 1024 * 1024, '\0');
        for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(i);
        if (!check(child.write(payload), "accept binary input")) return 1;
        std::string received;
        const auto deadline = GetTickCount64() + 5000;
        while ((child.pendingBytes() || received.size() < payload.size()) && GetTickCount64() < deadline) {
            if (!check(child.flush(), "flush without blocking on full pipes")) return 1;
            received += child.readOutput();
            Sleep(1);
        }
        if (!check(received == payload, "binary bytes survive pipe backpressure") ||
            !check(child.pendingBytes() == 0, "all bytes were written")) return 1;
        child.closeInput();
        if (!check(child.wait(3000), "EOF exits helper") ||
            !check(child.exitCode() == 17, "preserve exit code")) return 1;
        const auto errors = child.readErrors();
        if (!check(errors == "helper stderr\r\n" || errors == "helper stderr\n", "stderr pipe is separate")) return 1;
    }
    _putenv_s("MOONLIGHT_CLIPBOARD_PROCESS_TEST_CHILD", "hung");
    {
        WindowsClipboardProcess child;
        if (!check(child.start({path, pathLength}), "start hung helper")) return 1;
        const auto before = GetTickCount64();
        if (!check(child.write(std::string(4 * 1024 * 1024, 'x')), "queue data for hung helper")) return 1;
        for (int i = 0; i < 20; ++i) {
            if (!check(child.flush(), "pending write stays nonblocking")) return 1;
        }
        if (!check(child.pendingBytes() > 0, "backlog remains visible") ||
            !check(GetTickCount64() - before < 1000, "hung helper cannot stall caller")) return 1;
        child.closeInput();
        child.terminate();
        if (!check(child.wait(1000), "owned helper terminates with pending I/O")) return 1;
    }
    _putenv_s("MOONLIGHT_CLIPBOARD_PROCESS_TEST_CHILD", "");
    WindowsClipboardProcess missing;
    if (!check(!missing.start(L"Z:\\missing-helper.exe"), "missing executable fails cleanly")) return 1;
    std::puts("windows_clipboard_process=passed");
    return 0;
}
