#pragma once

#include <cstring>
#include <cstdlib>
#include <limits>

namespace StreamEnvironment {
inline bool space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

// Match the existing Qt integer overrides: ASCII whitespace, base detection,
// signed int range, full consumption, and zero on failure.
inline bool parseInteger(const char* text, int* value)
{
    *value = 0;
    if (!text) return false;
    while (space(*text)) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    unsigned base = 10;
    if (text[0] == '0') {
        base = 8;
        if (text[1] == 'x' || text[1] == 'X') { base = 16; text += 2; }
        else if (text[1] == 'b' || text[1] == 'B') { base = 2; text += 2; }
    }
    const unsigned limit = static_cast<unsigned>((std::numeric_limits<int>::max)()) + (negative ? 1U : 0U);
    unsigned result = 0;
    bool digits = false;
    for (; *text && !space(*text); ++text) {
        unsigned digit = *text >= '0' && *text <= '9' ? *text - '0' :
            *text >= 'a' && *text <= 'f' ? *text - 'a' + 10 :
            *text >= 'A' && *text <= 'F' ? *text - 'A' + 10 : 16;
        if (digit >= base || result > (limit - digit) / base) return false;
        result = result * base + digit;
        digits = true;
    }
    while (space(*text)) ++text;
    if (!digits || *text) return false;
    *value = negative ? (result == limit ? (std::numeric_limits<int>::min)() : -static_cast<int>(result))
                      : static_cast<int>(result);
    return true;
}

template<class T> bool getOverride(const char* name, T* value)
{
    int integer;
    const bool valid = parseInteger(std::getenv(name), &integer);
    *value = static_cast<T>(integer);
    return valid;
}

inline int integer(const char* name)
{
    int value;
    getOverride(name, &value);
    return value;
}
inline bool equals(const char* name, const char* expected)
{
    const char* value = std::getenv(name);
    return value && std::strcmp(value, expected) == 0;
}
}
