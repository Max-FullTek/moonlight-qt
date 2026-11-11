#include "windowsdisplaybrightness.h"
#include "SDL_compat.h"

#include <Windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <cwchar>

namespace {
std::string utf8(const std::wstring& text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length == 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), length, nullptr, nullptr)) return {};
    result.pop_back();
    return result;
}
}

std::wstring WindowsDisplayBrightness::displayDeviceName(int sdlDisplayIndex)
{
    SDL_Rect bounds{};
    if (sdlDisplayIndex < 0 || SDL_GetDisplayBounds(sdlDisplayIndex, &bounds) != 0) {
        return {};
    }

    // SDL's friendly name isn't the GDI device identifier used by DisplayConfig.
    // Require matching bounds to avoid publishing brightness for a different monitor.
    const RECT rect{bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h};
    const HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info) || !EqualRect(&rect, &info.rcMonitor)) {
        return {};
    }
    return info.szDevice;
}

float WindowsDisplayBrightness::querySdrWhiteNits(const std::wstring& displayName, bool logFailures)
{
    if (displayName.empty()) {
        return 0.0f;
    }

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    LONG status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,
                                               &pathCount, &modeCount);
    if (status != ERROR_SUCCESS) {
        if (!logFailures) {
            return 0.0f;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "GetDisplayConfigBufferSizes() failed for SDR white query: %ld",
                    status);
        return 0.0f;
    }

    // The topology can change between sizing and querying. Retry once with
    // fresh buffer sizes if Windows reports an insufficient buffer.
    for (int attempt = 0; attempt < 2; attempt++) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        UINT32 queriedPathCount = pathCount;
        UINT32 queriedModeCount = modeCount;
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                                    &queriedPathCount, paths.data(),
                                    &queriedModeCount, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) {
            status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,
                                                  &pathCount, &modeCount);
            if (status == ERROR_SUCCESS) {
                continue;
            }
        }
        if (status != ERROR_SUCCESS) {
            if (!logFailures) {
                return 0.0f;
            }
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "QueryDisplayConfig() failed for SDR white query: %ld",
                        status);
            return 0.0f;
        }

        paths.resize(queriedPathCount);
        for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
            sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sourceName.header.size = sizeof(sourceName);
            sourceName.header.adapterId = path.sourceInfo.adapterId;
            sourceName.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS ||
                    _wcsicmp(displayName.c_str(), sourceName.viewGdiDeviceName) != 0) {
                continue;
            }

            DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevel = {};
            whiteLevel.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
            whiteLevel.header.size = sizeof(whiteLevel);
            whiteLevel.header.adapterId = path.targetInfo.adapterId;
            whiteLevel.header.id = path.targetInfo.id;
            status = DisplayConfigGetDeviceInfo(&whiteLevel.header);
            if (status != ERROR_SUCCESS) {
                if (!logFailures) {
                    return 0.0f;
                }
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "DisplayConfigGetDeviceInfo(SDR_WHITE_LEVEL) failed for %s: %ld",
                            utf8(displayName).c_str(), status);
                return 0.0f;
            }

            // Windows stores this as a fixed-point multiplier where 1000 is
            // the standard 80-nit SDR white level.
            const float nits = static_cast<float>(whiteLevel.SDRWhiteLevel) * 80.0f / 1000.0f;
            if (nits < 50.0f || nits > 1000.0f) {
                if (!logFailures) {
                    return 0.0f;
                }
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Ignoring out-of-range SDR white level for %s: %.1f nits",
                            utf8(displayName).c_str(), nits);
                return 0.0f;
            }
            return nits;
        }

        if (logFailures) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to map display %s to an active DisplayConfig path",
                        utf8(displayName).c_str());
        }
        return 0.0f;
    }

    return 0.0f;
}

void WindowsDisplayBrightness::queryHdrBrightness(const std::wstring& preferredDisplayName,
                                        float& maxNits, float& minNits,
                                        float& maxFullNits, float& sdrWhiteNits)
{
    using Microsoft::WRL::ComPtr;

    maxNits = 0;
    minNits = 0;
    maxFullNits = 0;
    sdrWhiteNits = 0;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create DXGI factory for brightness query");
        return;
    }

    struct DisplayBrightness {
        std::wstring name;
        float maxNits = 0;
        float minNits = 0;
        float maxFullNits = 0;
        bool hdr = false;

        bool isValid() const { return !name.empty(); }
    } preferred, firstHdr, firstDisplay;

    // Prefer the display that owns the launch UI. If it cannot be matched,
    // retain the previous behavior of selecting the first HDR display.
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterIdx = 0; SUCCEEDED(factory->EnumAdapters1(adapterIdx, &adapter)); adapterIdx++) {
        ComPtr<IDXGIOutput> output;
        for (UINT outputIdx = 0; SUCCEEDED(adapter->EnumOutputs(outputIdx, &output)); outputIdx++) {
            ComPtr<IDXGIOutput6> output6;
            if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void**)&output6))) {
                DXGI_OUTPUT_DESC1 desc1;
                if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                    DisplayBrightness candidate;
                    candidate.name = desc1.DeviceName;
                    candidate.maxNits = desc1.MaxLuminance;
                    candidate.minNits = desc1.MinLuminance;
                    candidate.maxFullNits = desc1.MaxFullFrameLuminance;
                    candidate.hdr = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

                    if (!firstDisplay.isValid()) {
                        firstDisplay = candidate;
                    }
                    if (candidate.hdr && !firstHdr.isValid()) {
                        firstHdr = candidate;
                    }
                    if (!preferredDisplayName.empty() &&
                            _wcsicmp(candidate.name.c_str(), preferredDisplayName.c_str()) == 0) {
                        preferred = candidate;
                    }
                }
            }
            output.Reset();
        }
        adapter.Reset();
    }

    const DisplayBrightness selected = preferred.isValid() ? preferred :
                                       (firstHdr.isValid() ? firstHdr : firstDisplay);
    if (selected.isValid()) {
        maxNits = selected.maxNits;
        minNits = selected.minNits;
        maxFullNits = selected.maxFullNits;
        sdrWhiteNits = querySdrWhiteNits(selected.name);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Client display brightness (%s): max=%.1f nits, min=%.4f nits, maxFull=%.1f nits, SDR white=%.1f nits (HDR: %s)",
                    utf8(selected.name).c_str(), maxNits, minNits, maxFullNits,
                    sdrWhiteNits, selected.hdr ? "yes" : "no");
    } else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "No display brightness information available");
    }
}
