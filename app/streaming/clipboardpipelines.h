#pragma once

#include <istream>
#include <string>
#include <string_view>

namespace ClipboardPipe {

inline constexpr int MaxLineBytes = 1024 * 1024;
enum class LineStatus { Complete, Incomplete, Oversized };

struct LineBoundary {
    LineStatus status;
    size_t length;
};

LineBoundary findLine(std::string_view buffer);
// Reads one record, including a final unterminated record before EOF. An
// oversized record is drained without retaining it, so the next record can be read.
LineStatus readLine(std::istream& input, std::string& line);

}
