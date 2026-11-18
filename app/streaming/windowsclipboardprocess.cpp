#include "windowsclipboardprocess.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <vector>

namespace {
class Handle {
public:
    HANDLE value = nullptr;
    ~Handle() { reset(); }
    void reset(HANDLE next = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = next;
    }
};

std::string readPipe(HANDLE pipe)
{
    std::string result;
    // Bound each drain. The Session handles framing and the protocol limit.
    while (result.size() < 1024 * 1024) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
        char buffer[16384];
        DWORD received = 0;
        if (!ReadFile(pipe, buffer, (std::min)(available, DWORD(sizeof(buffer))), &received, nullptr) || !received) break;
        result.append(buffer, received);
    }
    return result;
}
}

struct WindowsClipboardProcess::Impl {
    Handle process, input, output, errors, writeEvent;
    OVERLAPPED overlapped{};
    std::string pending;
    bool writing = false;
    bool failed = false;

    void cancelWrite() {
        if (writing) {
            CancelIoEx(input.value, &overlapped);
            DWORD transferred;
            GetOverlappedResult(input.value, &overlapped, &transferred, TRUE);
            writing = false;
        }
    }
    ~Impl() { cancelWrite(); }
};

WindowsClipboardProcess::WindowsClipboardProcess() : m_Impl(std::make_unique<Impl>()) {}
WindowsClipboardProcess::~WindowsClipboardProcess()
{
    // Only this class's child may be terminated. Never leave it holding credentials.
    if (running()) { terminate(); wait(1000); }
}

bool WindowsClipboardProcess::start(const std::wstring& executable)
{
    if (m_Impl->process.value || executable.empty() || executable.find(L'"') != std::wstring::npos) return false;
    auto candidate = std::make_unique<Impl>();
    SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle childInput, childOutput, childErrors;
    static std::atomic<unsigned long> serial{0};
    const auto name = L"\\\\.\\pipe\\MoonlightClipboard-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(++serial);
    candidate->input.reset(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 65536, 65536, 0, nullptr));
    if (candidate->input.value == INVALID_HANDLE_VALUE) return false;
    childInput.reset(CreateFileW(name.c_str(), GENERIC_READ, 0, &inherited, OPEN_EXISTING, 0, nullptr));
    if (childInput.value == INVALID_HANDLE_VALUE) return false;
    // The client connected synchronously before the server begins writing.
    candidate->writeEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!candidate->writeEvent.value) return false;
    candidate->overlapped.hEvent = candidate->writeEvent.value;
    if (!ConnectNamedPipe(candidate->input.value, &candidate->overlapped)) {
        const auto error = GetLastError();
        if (error != ERROR_PIPE_CONNECTED) {
            if (error == ERROR_IO_PENDING) {
                CancelIoEx(candidate->input.value, &candidate->overlapped);
                DWORD transferred;
                GetOverlappedResult(candidate->input.value, &candidate->overlapped, &transferred, TRUE);
            }
            return false;
        }
    }
    if (!CreatePipe(&candidate->output.value, &childOutput.value, &inherited, 0) ||
        !SetHandleInformation(candidate->output.value, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&candidate->errors.value, &childErrors.value, &inherited, 0) ||
        !SetHandleInformation(candidate->errors.value, HANDLE_FLAG_INHERIT, 0)) return false;

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = childInput.value;
    startup.StartupInfo.hStdOutput = childOutput.value;
    startup.StartupInfo.hStdError = childErrors.value;
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<unsigned char> attributes(attributeSize);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize)) return false;
    HANDLE handles[]{childInput.value, childOutput.value, childErrors.value};
    const bool configured = UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr) != FALSE;
    PROCESS_INFORMATION information{};
    std::wstring command = L"\"" + executable + L"\"";
    const bool started = configured && CreateProcessW(executable.c_str(), command.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
        &startup.StartupInfo, &information) != FALSE;
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!started) return false;
    CloseHandle(information.hThread);
    candidate->process.reset(information.hProcess);
    m_Impl = std::move(candidate);
    return true;
}

bool WindowsClipboardProcess::running() const
{
    return m_Impl->process.value && WaitForSingleObject(m_Impl->process.value, 0) == WAIT_TIMEOUT;
}
bool WindowsClipboardProcess::wait(unsigned timeoutMs) const
{
    return m_Impl->process.value && WaitForSingleObject(m_Impl->process.value, timeoutMs) == WAIT_OBJECT_0;
}
unsigned WindowsClipboardProcess::exitCode() const
{
    DWORD result = 0;
    if (m_Impl->process.value) GetExitCodeProcess(m_Impl->process.value, &result);
    return result;
}
bool WindowsClipboardProcess::write(std::string_view bytes)
{
    if (m_Impl->failed || !m_Impl->input.value || !running()) return false;
    // Caller owns the bounded queue. Never resize the buffer used by a pending write.
    if (!m_Impl->pending.empty()) return false;
    m_Impl->pending.assign(bytes);
    return flush();
}
bool WindowsClipboardProcess::flush()
{
    auto& state = *m_Impl;
    if (state.failed || !state.input.value) return false;
    DWORD written = 0;
    if (state.writing) {
        if (!GetOverlappedResult(state.input.value, &state.overlapped, &written, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) return true;
            state.failed = true;
            state.writing = false;
            return false;
        }
        state.writing = false;
        state.pending.erase(0, written);
    }
    while (!state.pending.empty()) {
        ResetEvent(state.writeEvent.value);
        const DWORD count = static_cast<DWORD>((std::min)(state.pending.size(), size_t(65536)));
        if (!WriteFile(state.input.value, state.pending.data(), count, &written, &state.overlapped)) {
            if (GetLastError() == ERROR_IO_PENDING) { state.writing = true; return true; }
            state.failed = true;
            return false;
        }
        state.pending.erase(0, written);
    }
    return true;
}
size_t WindowsClipboardProcess::pendingBytes() const { return m_Impl->pending.size(); }
std::string WindowsClipboardProcess::readOutput() { return readPipe(m_Impl->output.value); }
std::string WindowsClipboardProcess::readErrors() { return readPipe(m_Impl->errors.value); }
void WindowsClipboardProcess::closeInput()
{
    m_Impl->cancelWrite();
    m_Impl->pending.clear();
    m_Impl->input.reset();
}
void WindowsClipboardProcess::terminate()
{
    if (running()) TerminateProcess(m_Impl->process.value, 1);
}
