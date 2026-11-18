#include "workerpreferences.h"
#include "windowsprofilefile.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
std::filesystem::path profileDirectory;
constexpr size_t MaxPreferencesBytes = 4 * 1024 * 1024;

std::filesystem::path pathFor(WorkerPreferences::Section section)
{
    return profileDirectory / (section == WorkerPreferences::Section::Streaming ?
        L"stream-settings-v1.json" : L"controller-mappings-v1.json");
}
}

void WorkerPreferences::initialize(std::filesystem::path directory)
{
    profileDirectory = std::move(directory);
}

const std::filesystem::path& WorkerPreferences::directory()
{
    return profileDirectory;
}

LegacyWorkerProfile::Values WorkerPreferences::load(Section section, bool* imported)
{
    if (imported) *imported = false;
    const auto data = WindowsProfileFile::read(pathFor(section), MaxPreferencesBytes);
    if (data) {
        const auto record = nlohmann::json::parse(*data);
        if (!record.at("version").is_number_integer() || record.at("version") != 1) {
            throw std::runtime_error("Unsupported worker preference version");
        }
        return record.at("values").get<LegacyWorkerProfile::Values>();
    }

    LegacyWorkerProfile::Values values;
    for (const auto& [key, value] : LegacyWorkerProfile::read(profileDirectory)) {
        if (section == Section::ControllerMappings) {
            constexpr std::string_view prefix = "gcmapping/";
            if (key.compare(0, prefix.size(), prefix) == 0) values[key.substr(prefix.size())] = value;
        } else if (key.find('/') == std::string::npos &&
                   key != "uniqueid" && key != "certificate" && key != "key") {
            values[key] = value;
        }
    }
    if (imported) *imported = !values.empty();
    return values;
}

void WorkerPreferences::save(Section section, const LegacyWorkerProfile::Values& values)
{
    const auto data = nlohmann::json{{"version", 1}, {"values", values}}.dump();
    if (data.size() > MaxPreferencesBytes) throw std::runtime_error("Worker preferences too large");
    WindowsProfileFile::replace(pathFor(section), data);
}
