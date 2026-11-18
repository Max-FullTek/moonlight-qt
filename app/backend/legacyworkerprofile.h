#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace LegacyWorkerProfile {
using Values = std::map<std::string, std::string, std::less<>>;
Values parseIni(std::string_view data);
// Reads only the old isolated GPUI profile, including QSettings byte strings
// used for certificates and MACs. Never writes to that profile.
const Values& read(const std::filesystem::path& directory);
std::string_view value(const Values& values, std::string_view key);
int number(const Values& values, std::string_view key, int fallback = 0);
}
