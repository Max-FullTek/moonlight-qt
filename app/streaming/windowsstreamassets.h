#pragma once

#include <filesystem>
#include <string>
#include <vector>

std::string readWindowsStreamAsset(const char* name,
                                   const std::filesystem::path& cacheDir,
                                   const std::filesystem::path& portableRoot,
                                   const std::vector<std::filesystem::path>& appDataDirs,
                                   const std::filesystem::path& applicationDir);
