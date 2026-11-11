#pragma once

#include <charconv>
#include <string_view>
#include <vector>

namespace NativeVersion {

using Segments = std::vector<int>;

// The caller trims surrounding whitespace. Qt accepts ASCII whitespace and
// a plus sign before each component. Require a complete, nonnegative int.
inline Segments parseNumeric(std::string_view version)
{
    Segments result;
    while (!version.empty()) {
        const auto separator = version.find('.');
        auto part = version.substr(0, separator);
        const auto first = part.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string_view::npos) {
            return {};
        }
        part.remove_prefix(first);
        if (part.front() == '+') {
            part.remove_prefix(1);
        }
        if (part.empty() || part.front() < '0' || part.front() > '9') {
            return {};
        }
        int value = 0;
        const auto end = part.data() + part.size();
        const auto parsed = std::from_chars(part.data(), end, value);
        if (parsed.ec != std::errc() || parsed.ptr != end) {
            return {};
        }
        result.push_back(value);
        if (separator == std::string_view::npos) {
            return result;
        }
        version.remove_prefix(separator + 1);
    }
    return {};
}

inline int compare(const Segments& lhs, const Segments& rhs)
{
    for (std::size_t i = 0; i < lhs.size() || i < rhs.size(); ++i) {
        const int left = i < lhs.size() ? lhs[i] : 0;
        const int right = i < rhs.size() ? rhs[i] : 0;
        if (left != right) {
            return left < right ? -1 : 1;
        }
    }
    return 0;
}

} // namespace NativeVersion
