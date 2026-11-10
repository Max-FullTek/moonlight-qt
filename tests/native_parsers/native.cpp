// Capability probe only. The production worker still uses its Qt parsers.
#include <expat.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <limits>
#include <cmath>
#include <climits>
using Json = nlohmann::json;
struct DepthLimit {};

std::string unhex(const std::string& input) {
    std::string result;
    for (size_t i = 0; i < input.size(); i += 2) result += static_cast<char>(std::stoi(input.substr(i, 2), nullptr, 16));
    return result;
}
struct Xml {
    XML_Parser parser;
    Json events = Json::array();
    bool callbackFailed = false;
    template<class F> void safely(F callback) noexcept {
        try { callback(); }
        catch (...) { callbackFailed = true; XML_StopParser(parser, XML_FALSE); }
    }
    static void XMLCALL start(void* context, const XML_Char* name, const XML_Char** attributes) {
        auto& xml = *static_cast<Xml*>(context);
        xml.safely([&] {
            Json values = Json::object();
            for (auto p = attributes; *p; p += 2) values[p[0]] = p[1];
            xml.events.push_back(Json::array({"start", name, values}));
        });
    }
    static void XMLCALL end(void* context, const XML_Char* name) {
        auto& xml = *static_cast<Xml*>(context);
        xml.safely([&] { xml.events.push_back(Json::array({"end", name})); });
    }
    static void XMLCALL text(void* context, const XML_Char* data, int size) {
        auto& xml = *static_cast<Xml*>(context);
        xml.safely([&] {
            if (!xml.events.empty() && xml.events.back()[0] == "text")
                xml.events.back()[1].get_ref<std::string&>().append(data, size);
            else xml.events.push_back(Json::array({"text", std::string(data, size)}));
        });
    }
    static int XMLCALL external(XML_Parser, const XML_Char*, const XML_Char*, const XML_Char*, const XML_Char*) {
        return XML_STATUS_ERROR; // Never fetch URLs or local files.
    }
};
int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request = Json::parse(line);
        const auto body = unhex(request.at("hex").get<std::string>());
        Json result;
        if (request.at("kind") == "xml") {
            std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)> parser(XML_ParserCreateNS("UTF-8", '\x1f'), XML_ParserFree);
            if (!parser) return 2;
            Xml xml{parser.get()};
            XML_SetUserData(parser.get(), &xml);
            XML_SetElementHandler(parser.get(), Xml::start, Xml::end);
            XML_SetCharacterDataHandler(parser.get(), Xml::text);
            XML_SetExternalEntityRefHandler(parser.get(), Xml::external);
            const bool valid = body.size() <= INT_MAX && XML_Parse(parser.get(), body.data(), static_cast<int>(body.size()), XML_TRUE) == XML_STATUS_OK && !xml.callbackFailed;
            result = {{"valid", valid}};
            if (valid) result["events"] = std::move(xml.events);
        } else {
            // Candidate policy only, pending SA. Unbounded nesting overflowed
            // the Debug DOM traversal stack in the capability probe. Reject
            // during parsing, before constructing a deep value tree.
            Json json;
            try {
                json = Json::parse(body, [](int depth, Json::parse_event_t, Json&) {
                    if (depth > 64) throw DepthLimit{};
                    return true;
                }, false);
            } catch (const DepthLimit&) { json = Json(Json::value_t::discarded); }
            const bool valid = !json.is_discarded() && json.is_object();
            result = {{"valid", valid}};
            if (valid) {
                result["value"] = json;
                const auto found = json.find("number");
                int integer = 0;
                // QJsonValue::toInt requires an exact integral value in range.
                if (found != json.end() && found->is_number()) {
                    const auto number = found->get<double>();
                    if (std::isfinite(number) && std::trunc(number) == number && number >= INT_MIN && number <= INT_MAX)
                        integer = static_cast<int>(number);
                }
                result["integer"] = integer;
            }
        }
        std::cout << result.dump() << '\n' << std::flush;
    }
}
