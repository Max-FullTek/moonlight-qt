#pragma once

#include <cstdint>
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
}
