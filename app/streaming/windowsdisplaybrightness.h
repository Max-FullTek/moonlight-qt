#pragma once

#include <string>

namespace WindowsDisplayBrightness {
std::wstring displayDeviceName(int sdlDisplayIndex);
float querySdrWhiteNits(const std::wstring& displayName, bool logFailures = true);
void queryHdrBrightness(const std::wstring& preferredDisplayName,
                        float& maxNits, float& minNits,
                        float& maxFullNits, float& sdrWhiteNits);
}
