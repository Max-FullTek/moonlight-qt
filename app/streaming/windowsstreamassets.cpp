#include "windowsstreamassets.h"

#include <windows.h>
#include <fstream>
#include <iterator>

namespace {
std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

std::string readWindowsStreamAsset(const char* name,
                                   const std::filesystem::path& cacheDir,
                                   const std::filesystem::path& portableRoot,
                                   const std::vector<std::filesystem::path>& appDataDirs,
                                   const std::filesystem::path& applicationDir)
{
    const std::filesystem::path fileName(name);
    std::error_code error;
    for (const auto& directory : {cacheDir, portableRoot, std::filesystem::path(L".")}) {
        const auto candidate = directory / fileName;
        if (std::filesystem::exists(candidate, error)) return readFile(candidate);
    }
    for (const auto& directory : appDataDirs) {
        const auto candidate = directory / fileName;
        if (std::filesystem::is_regular_file(candidate, error)) return readFile(candidate);
    }
    const auto installed = applicationDir / fileName;
    if (std::filesystem::exists(installed, error)) return readFile(installed);

    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, fileName.c_str(), MAKEINTRESOURCEW(10));
    if (!resource) return {};
    const auto data = static_cast<const char*>(LockResource(LoadResource(module, resource)));
    return data ? std::string(data, SizeofResource(module, resource)) : std::string();
}
