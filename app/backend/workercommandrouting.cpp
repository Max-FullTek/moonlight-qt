#include "workercommandrouting.h"

#include <cmath>
#include <nlohmann/json.hpp>

WorkerCommandRouting::Route WorkerCommandRouting::inspect(std::string_view line)
{
    using Json = nlohmann::json;
    bool tooDeep = false;
    const auto object = Json::parse(line.begin(), line.end(),
        [&tooDeep](int depth, Json::parse_event_t, Json&) {
            if (depth >= 1024) tooDeep = true;
            return !tooDeep;
        }, false);
    if (tooDeep || !object.is_object()) return {};
    const auto command = object.find("command");
    if (command == object.end()) return {};
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
