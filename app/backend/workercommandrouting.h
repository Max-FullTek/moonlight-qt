#pragma once

#include "workercommandpipe.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace WorkerCommandRouting {
enum class Kind { Command, Shutdown, SessionApiResult };

struct Route {
    Kind kind = Kind::Command;
    uint64_t requestId = 0;
    std::string result;
};

Route inspect(std::string_view line);
// Commands are delivered on the reader thread. The caller schedules owner work.
void run(std::FILE* input, const std::function<void(WorkerCommandPipe::ReadStatus, std::string)>& enqueue);
}
