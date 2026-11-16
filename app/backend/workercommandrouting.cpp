#include "workercommandrouting.h"
#include "sessionapirpc.h"

#include <cmath>
#include <nlohmann/json.hpp>

WorkerCommandRouting::Route WorkerCommandRouting::inspect(std::string_view line)
{
    using Json = nlohmann::json;
    if (line.find('\0') != std::string_view::npos) return {Kind::InvalidJson, 0, {}};
    bool tooDeep = false;
    const auto object = Json::parse(line.begin(), line.end(),
        [&tooDeep](int depth, Json::parse_event_t, Json&) {
            if (depth >= 1024) tooDeep = true;
            return !tooDeep;
        }, false);
    if (tooDeep || !object.is_object()) return {Kind::InvalidJson, 0, {}};
    const auto command = object.find("command");
    if (command == object.end()) return {};
    if (*command == "monitor_update") return {Kind::MonitorUpdate, 0, {}};
    if (*command == "shutdown") return {Kind::Shutdown, 0, {}};
    if (*command != "session_api_result") return {};

    Route route{Kind::SessionApiResult, 0, {}};
    const auto id = object.find("request_id");
    const auto result = object.find("result");
    if (id == object.end() || !id->is_number() || result == object.end() || !result->is_object()) {
        return route;
    }
    const double number = id->get<double>();
    if (number >= 1 && number <= 9007199254740991.0 && std::floor(number) == number) {
        route.requestId = static_cast<uint64_t>(number);
        route.result = result->dump();
    }
    return route;
}

void WorkerCommandRouting::run(
    std::FILE* input, const std::function<void(Input, std::string)>& enqueue)
{
    using WorkerCommandPipe::ReadStatus;
    std::string line;
    for (;;) {
        const auto status = WorkerCommandPipe::read(input, line);
        if (status != ReadStatus::Command) {
            if (status == ReadStatus::EndOfInput) {
                SessionApiRpc::broker().close();
                enqueue(Input::EndOfInput, {});
                return;
            }
            enqueue(Input::Oversized, {});
            continue;
        }
        auto route = inspect(line);
        if (route.kind == Kind::SessionApiResult) {
            if (route.requestId) SessionApiRpc::broker().complete(route.requestId, std::move(route.result));
            continue;
        }
        if (line.size() > WorkerCommandPipe::MaxControlLineBytes && route.kind != Kind::MonitorUpdate) {
            enqueue(Input::Oversized, {});
            continue;
        }
        if (route.kind == Kind::InvalidJson) {
            enqueue(Input::InvalidJson, {});
            continue;
        }
        const bool shutdown = route.kind == Kind::Shutdown;
        if (shutdown) SessionApiRpc::broker().close();
        enqueue(shutdown ? Input::Shutdown : Input::Command, std::move(line));
        if (shutdown) return;
    }
}
