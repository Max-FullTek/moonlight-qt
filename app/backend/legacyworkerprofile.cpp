#include "legacyworkerprofile.h"
#include "windowsprofilefile.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <charconv>
#include <stdexcept>

namespace {
std::string_view trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r");
    if (first == value.npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string decodeKey(std::string_view value)
{
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const auto high = hex(value[i + 1]), low = hex(value[i + 2]);
            if (high >= 0 && low >= 0) {
                const auto code = high * 16 + low;
                if (code < 128) {
                    result += static_cast<char>(code);
                } else {
                    result += static_cast<char>(0xc0 | (code >> 6));
                    result += static_cast<char>(0x80 | (code & 0x3f));
                }
                i += 2;
                continue;
            }
            std::wstring unicode;
            while (i + 5 < value.size() && value.substr(i, 2) == "%U") {
                unsigned code = 0;
                const auto first = value.data() + i + 2;
                const auto converted = std::from_chars(first, first + 4, code, 16);
                if (converted.ptr != first + 4 || converted.ec != std::errc{}) break;
                unicode += static_cast<wchar_t>(code);
                i += 6;
            }
            if (!unicode.empty()) {
                const int count = static_cast<int>(unicode.size());
                const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, unicode.data(), count,
                                                       nullptr, 0, nullptr, nullptr);
                if (!bytes) throw std::runtime_error("Invalid legacy profile key");
                const auto start = result.size();
                result.resize(start + bytes);
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, unicode.data(), count,
                                    result.data() + start, bytes, nullptr, nullptr);
                --i;
                continue;
            }
        }
        result += value[i] == '\\' ? '/' : value[i];
    }
    return result;
}

std::string decodeValue(std::string_view value)
{
    value = trim(value);
    bool quoted = false;
    size_t significantSize = 0;
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (c == ';' && !quoted) {
            result.resize(significantSize);
            break;
        }
        if (c != '\\' || i + 1 == value.size()) {
            result += c;
            if (quoted || (c != ' ' && c != '\t')) significantSize = result.size();
            continue;
        }
        c = value[++i];
        switch (c) {
        case 'a': result += '\a'; break;
        case 'b': result += '\b'; break;
        case 'f': result += '\f'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'v': result += '\v'; break;
        case 'x': {
            unsigned byte = 0;
            while (i + 1 < value.size() && hex(value[i + 1]) >= 0) {
                byte = byte * 16 + hex(value[++i]);
            }
            result += static_cast<char>(byte);
            break;
        }
        default:
            if (c >= '0' && c <= '7') {
                unsigned byte = c - '0';
                for (int digits = 1; digits < 3 && i + 1 < value.size() &&
                    value[i + 1] >= '0' && value[i + 1] <= '7'; ++digits) {
                    byte = byte * 8 + value[++i] - '0';
                }
                result += static_cast<char>(byte);
            } else {
                result += c;
            }
        }
        significantSize = result.size();
    }
    if (result.compare(0, 11, "@ByteArray(") == 0 && result.back() == ')') {
        return result.substr(11, result.size() - 12);
    }
    if (result == "@Invalid()") return {};
    if (result.compare(0, 8, "@String(") == 0 && result.back() == ')') {
        return result.substr(8, result.size() - 9);
    }
    if (result.compare(0, 2, "@@") == 0) result.erase(0, 1);
    return result;
}

LegacyWorkerProfile::Values parse(std::string_view data)
{
    if (data.substr(0, 3) == "\xef\xbb\xbf") data.remove_prefix(3);
    LegacyWorkerProfile::Values values;
    std::string section;
    while (!data.empty()) {
        const auto end = data.find('\n');
        const auto line = trim(data.substr(0, end));
        data.remove_prefix(end == data.npos ? data.size() : end + 1);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            const auto group = line.substr(1, line.size() - 2);
            section = group == "General" ? "" : decodeKey(group) + '/';
            continue;
        }
        const auto equals = line.find('=');
        if (equals == line.npos) continue;
        values[section + decodeKey(trim(line.substr(0, equals)))] = decodeValue(line.substr(equals + 1));
    }
    return values;
}
}

const LegacyWorkerProfile::Values& LegacyWorkerProfile::read(const std::filesystem::path& directory)
{
    // The isolated worker root is configured once at startup.
    static const auto values = [&directory] {
        const auto file = WindowsProfileFile::read(directory /
            L"Moonlight Game Streaming Project" / L"MoonlightGPUI.ini", 64 * 1024 * 1024);
        return file ? parse(*file) : Values{};
    }();
    return values;
}

std::string_view LegacyWorkerProfile::value(const Values& values, std::string_view key)
{
    const auto found = values.find(key);
    return found == values.end() ? std::string_view{} : std::string_view(found->second);
}

int LegacyWorkerProfile::number(const Values& values, std::string_view key, int fallback)
{
    auto text = value(values, key);
    if (text.empty()) return fallback;
    if (text.front() == '+') text.remove_prefix(1);
    int result;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? result : fallback;
}
