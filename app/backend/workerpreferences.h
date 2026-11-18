#pragma once

#include "legacyworkerprofile.h"

namespace WorkerPreferences {
void initialize(std::filesystem::path directory);
const std::filesystem::path& directory();
enum class Section { Streaming, ControllerMappings };
LegacyWorkerProfile::Values load(Section section, bool* imported = nullptr);
void save(Section section, const LegacyWorkerProfile::Values& values);
}
