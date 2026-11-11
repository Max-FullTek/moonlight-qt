#include "streaming/input/input.h"
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
class Owner final : public IInputHost
{
public:
    int fullscreen = 0, minimized = 0, statistics = 0, ended = 0, mappings = 0;
    bool isStreamingWindowFullscreen() const override { return false; }
    bool usesCompositorWindow() const override { return false; }
    bool shouldConfinePointerForCompositor() const override { return false; }
    void toggleFullscreen() override { ++fullscreen; }
    void minimizeStreamingWindow() override { ++minimized; }
    void toggleStatisticsOverlay() override { ++statistics; }
    void quitHostAndExit() override { ++ended; }
    void showInputControls() override {}
    void notifyMouseEmulationMode(bool) override {}
    bool queueTouchpadFlush() override { return true; }
    bool queueCursorFlush() override { return true; }
    void applyControllerMappings() override
    {
        require(SDL_WasInit(SDL_INIT_JOYSTICK) != 0, "mapping follows joystick initialization");
        require(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0, "mapping precedes controller initialization");
        ++mappings;
    }
    std::optional<uint16_t> sensorReportRateLimit() const override { return 60; }
};
void expectQuit()
{
    SDL_Event event{};
    require(SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 1,
            "disconnect posts an SDL quit event");
}
}
int main()
{
    SDL_SetMainReady();
    require(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER) == 0, SDL_GetError());
    // Exercise the real handler without a streaming host or desktop window.
    Owner owner;
    InputOptions options{};
    options.desktopEnvironment = true;
    options.backgroundGamepad = true;
    for (int attempt = 0; attempt < 2; ++attempt) {
        {
            SdlInputHandler input(owner, options, 1920, 1080, false);
            input.performSpecialKeyCombo(SdlInputHandler::KeyComboToggleFullScreen);
            input.performSpecialKeyCombo(SdlInputHandler::KeyComboToggleMinimize);
            input.performSpecialKeyCombo(SdlInputHandler::KeyComboToggleStatsOverlay);
            input.performSpecialKeyCombo(SdlInputHandler::KeyComboQuit);
            expectQuit();
            require(owner.ended == attempt, "temporary disconnect does not end host session");
            input.performSpecialKeyCombo(SdlInputHandler::KeyComboQuitAndExit);
            expectQuit();
            require(owner.ended == attempt + 1, "full disconnect marks host session for termination");
        }
        require(SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0,
                "handler releases SDL controller subsystems before retry");
        require(owner.fullscreen == attempt + 1 && owner.minimized == attempt + 1 &&
                owner.statistics == attempt + 1 && owner.mappings == attempt + 1,
                "window actions reach the current owner exactly once");
    }
    int mappingCalls = 0;
    SdlInputHandler::getUnmappedGamepads([&] { ++mappingCalls; });
    require(mappingCalls == 1, "controller discovery uses supplied mappings");
    require(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0, "discovery releases SDL subsystem");
    SDL_Quit();
    std::puts("native input owner: passed (no Qt)");
    return 0;
}
