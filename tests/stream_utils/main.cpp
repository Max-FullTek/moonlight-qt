#include "streaming/streamutils.h"

#include <intrin.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s; SDL: %s\n", message, SDL_GetError());
        std::exit(1);
    }
}

SDL_AssertState SDLCALL reportAssertion(const SDL_AssertData* data, void*)
{
    std::fprintf(stderr, "SDL assertion: %s (%s:%d)\n",
                 data->condition, data->filename, data->linenum);
    std::_Exit(2);
}

bool near(float lhs, float rhs)
{
    return std::abs(lhs - rhs) < 0.00001f;
}
}

int main()
{
    SDL_SetMainReady();
    SDL_SetAssertionHandler(reportAssertion, nullptr);
    int cpu[4];
    __cpuid(cpu, 1);
    require(StreamUtils::hasFastAes() == ((cpu[2] & (1 << 25)) != 0), "AES-NI CPU detection");
    require(StreamUtils::getPlatformWindowFlags() == SDL_WINDOW_VULKAN,
            "preserve Vulkan window-probing preference");

    SDL_Rect source{0, 0, 1920, 1080};
    SDL_Rect destination{100, 200, 1000, 1000};
    StreamUtils::scaleSourceToDestinationSurface(&source, &destination);
    require(destination.x == 100 && destination.y == 418 &&
            destination.w == 1000 && destination.h == 563, "landscape letterboxing");
    source = {0, 0, 1080, 1920};
    destination = {100, 200, 1000, 1000};
    StreamUtils::scaleSourceToDestinationSurface(&source, &destination);
    require(destination.x == 318 && destination.y == 200 &&
            destination.w == 563 && destination.h == 1000, "portrait letterboxing");
    SDL_Rect integerRect{480, 270, 960, 540};
    SDL_FRect normalized{};
    StreamUtils::screenSpaceToNormalizedDeviceCoords(&integerRect, &normalized, 1920, 1080);
    require(near(normalized.x, -0.5f) && near(normalized.y, -0.5f) &&
            near(normalized.w, 1.0f) && near(normalized.h, 1.0f), "integer device coordinates");
    SDL_FRect floatRect{480, 270, 960, 540};
    StreamUtils::screenSpaceToNormalizedDeviceCoords(&floatRect, 1920, 1080);
    require(near(floatRect.x, normalized.x) && near(floatRect.y, normalized.y) &&
            near(floatRect.w, normalized.w) && near(floatRect.h, normalized.h),
            "both coordinate overloads agree");

    require(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0, "select isolated video driver");
    require(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, "initialize dummy video");
    require(SDL_strcmp(SDL_GetCurrentVideoDriver(), "dummy") == 0, "never use real display");
    SDL_Window* window = StreamUtils::createTestWindow();
    require(window != nullptr, "hidden probe falls back when Vulkan is unavailable");
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) != 0, "probe remains hidden");
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    require(width == 1280 && height == 720, "probe dimensions");
    SDL_DisplayMode desktop{}, expected{};
    SDL_Rect safe{};
    require(SDL_GetDesktopDisplayMode(0, &expected) == 0, "dummy desktop mode");
    require(StreamUtils::getNativeDesktopMode(0, &desktop, &safe), "query native desktop");
    require(desktop.w == expected.w && desktop.h == expected.h && safe.x == 0 &&
            safe.y == 0 && safe.w == desktop.w && safe.h == desktop.h, "desktop safe area");
    require(!StreamUtils::getNativeDesktopMode(SDL_GetNumVideoDisplays(), &desktop, &safe),
            "reject out-of-range display");
    SDL_DisplayMode current{};
    require(SDL_GetCurrentDisplayMode(0, &current) == 0, "dummy current display mode");
    require(StreamUtils::getDisplayRefreshRate(window) ==
            (current.refresh_rate == 0 ? 60 : current.refresh_rate), "refresh-rate fallback");
    bool mustClose = true;
    require(StreamUtils::getDrmFdForWindow(window, &mustClose) == -1 && !mustClose &&
            StreamUtils::getDrmFd(true) == -1 && StreamUtils::getDrmFd(false) == -1,
            "Windows never opens DRM devices");
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    require(SDL_WasInit(SDL_INIT_VIDEO) == 0, "release video subsystem");
    std::puts("stream_utils=passed (dummy video, no physical display)");
}
