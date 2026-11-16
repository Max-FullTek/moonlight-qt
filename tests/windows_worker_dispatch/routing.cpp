#include "backend/workercommandrouting.h"

int main()
{
    using WorkerCommandRouting::inspect;
    using WorkerCommandRouting::Kind;
    if (inspect(R"({"command":"shutdown"})" "\r").kind != Kind::Shutdown) return 1;
    if (inspect(R"({"command":"shutdown"} trailing)").kind != Kind::Command) return 2;
    if (inspect(R"({"command":"shutdown","command":"hosts"})").kind != Kind::Command) return 3;
    const auto response = inspect(R"({"command":"session_api_result","request_id":9007199254740991,"result":{"ok":true}})");
    if (response.kind != Kind::SessionApiResult || response.requestId != 9007199254740991ULL ||
        response.result != R"({"ok":true})") return 4;
    for (auto input : {
        R"({"command":"session_api_result","request_id":9007199254740992,"result":{}})",
        R"({"command":"session_api_result","request_id":0,"result":{}})",
        R"({"command":"session_api_result","request_id":1.5,"result":{}})",
        R"({"command":"session_api_result","request_id":"1","result":{}})",
        R"({"command":"session_api_result","request_id":1,"result":[]})"}) {
        const auto invalid = inspect(input);
        if (invalid.kind != Kind::SessionApiResult || invalid.requestId || !invalid.result.empty()) return 5;
    }
    const std::string nested = R"({"command":"shutdown","extra":)" + std::string(2048, '[') +
        "0" + std::string(2048, ']') + "}";
    if (inspect(nested).kind != Kind::Command) return 6;
    return 0;
}
