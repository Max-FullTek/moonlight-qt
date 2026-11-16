#pragma once

#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace WorkerCommandPipe {
inline constexpr size_t MaxLineBytes = 4 * 1024 * 1024;
inline constexpr size_t MaxControlLineBytes = 64 * 1024;
inline constexpr std::string_view Ready = R"({"event":"ready","protocol":1,"qml_loaded":false})";
enum class ReadStatus { Command, Oversized, EndOfInput };

inline bool write(std::string_view record)
{
    static std::mutex outputMutex;
    std::lock_guard lock(outputMutex);
    return std::fwrite(record.data(), 1, record.size(), stdout) == record.size() &&
           std::fputc('\n', stdout) != EOF && std::fflush(stdout) == 0;
}

// Commands require a terminating newline. Drain an oversized record before
// returning so it cannot turn into commands on the following read.
inline ReadStatus read(std::FILE* input, std::string& line)
{
    line.clear();
    bool oversized = false;
    for (;;) {
        const int byte = std::fgetc(input);
        if (byte == EOF) {
            line.clear();
            return ReadStatus::EndOfInput;
        }
        if (byte == '\n') return oversized ? ReadStatus::Oversized : ReadStatus::Command;
        if (line.size() < MaxLineBytes) line.push_back(static_cast<char>(byte));
        else oversized = true;
    }
}
}
