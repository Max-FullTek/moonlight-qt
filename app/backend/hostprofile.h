#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

class NvComputer;
class QString;

namespace HostProfile {
std::optional<std::vector<std::unique_ptr<NvComputer>>> read(const std::filesystem::path& path);
void save(const std::filesystem::path& path, const std::vector<NvComputer>& hosts);
QString pairName(const QString& directory, const QString& uuid);
void setPairName(const QString& directory, const QString& uuid, const QString& name);
}
