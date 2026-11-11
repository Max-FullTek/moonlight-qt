#pragma once

#include <cstdio>
#include <string>

namespace WorkerCommandPipe {
inline constexpr size_t MaxLineBytes = 4 * 1024 * 1024;
enum class ReadStatus { Command, Oversized, EndOfInput };

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
