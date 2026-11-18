#include "windowsprofilefile.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <stdexcept>

namespace {
class File {
public:
    explicit File(HANDLE handle) : handle(handle) {}
    ~File() { close(); }
    void close() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }
    HANDLE handle;
};
}

std::optional<std::string> WindowsProfileFile::read(const std::filesystem::path& path, size_t limit)
{
    File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("Unable to open profile file");
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file.handle, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > limit || size.QuadPart > MAXDWORD) {
        throw std::runtime_error("Invalid profile file size");
    }
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD count;
    if (!ReadFile(file.handle, data.data(), static_cast<DWORD>(data.size()), &count, nullptr) ||
        count != data.size()) {
        throw std::runtime_error("Unable to read profile file");
    }
    return data;
}

void WindowsProfileFile::replace(const std::filesystem::path& path, std::string_view data)
{
    static std::atomic<unsigned long> sequence{0};
    auto pending = path;
    pending += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
    File file(CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Unable to create profile file");
    DWORD count;
    const bool written = data.size() <= MAXDWORD &&
        WriteFile(file.handle, data.data(), static_cast<DWORD>(data.size()), &count, nullptr) &&
        count == data.size() && FlushFileBuffers(file.handle);
    file.close();
    if (!written || !MoveFileExW(pending.c_str(), path.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(pending.c_str());
        throw std::runtime_error("Unable to replace profile file");
    }
}
