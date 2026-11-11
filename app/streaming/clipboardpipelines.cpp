#include "clipboardpipelines.h"

namespace ClipboardPipe {

LineBoundary findLine(std::string_view buffer)
{
    const auto end = buffer.find('\n');
    const auto length = end == std::string_view::npos ? buffer.size() : end;
    if (length > MaxLineBytes) return {LineStatus::Oversized, 0};
    return {end == std::string_view::npos ? LineStatus::Incomplete : LineStatus::Complete, length};
}

LineStatus readLine(std::istream& input, std::string& line)
{
    line.clear();
    using Traits = std::char_traits<char>;
    auto* buffer = input.rdbuf();
    bool oversized = false;
    for (;;) {
        const auto value = buffer->sbumpc();
        if (Traits::eq_int_type(value, Traits::eof())) {
            if (line.empty() && !oversized) return LineStatus::Incomplete;
            break;
        }
        const char byte = Traits::to_char_type(value);
        if (byte == '\n') break;
        // A terminating CR is removed before applying the existing protocol limit.
        if (line.size() <= MaxLineBytes) line.push_back(byte);
        else oversized = true;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (oversized || line.size() > MaxLineBytes) {
        line.clear();
        return LineStatus::Oversized;
    }
    return LineStatus::Complete;
}

}
