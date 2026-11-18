#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace WindowsProfileFile {
// An absent file permits legacy import. Any other read failure is reported.
std::optional<std::string> read(const std::filesystem::path& path, size_t limit);
void replace(const std::filesystem::path& path, std::string_view data);
}
