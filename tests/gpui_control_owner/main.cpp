// Isolated UI fixture: no network, host, pairing, or remote input. Copy this
// executable as Moonlight.exe beside the real frontend in a disposable folder.
#define NOMINMAX
#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <memory>
#include <filesystem>

void event(const char* body) { std::cout << "{\"protocol\":1," << body << "}\n" << std::flush; }
void controls(bool active) {
    event(active ? "\"event\":\"controls\",\"active\":true,\"gamepad_mouse\":false,\"has_gamepad\":true"
                 : "\"event\":\"controls\",\"active\":false,\"gamepad_mouse\":false,\"has_gamepad\":true");
}
int main(int argc, char**) {
    if (argc == 1) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        auto root = std::filesystem::path(path).parent_path();
        SetEnvironmentVariableW(L"LOCALAPPDATA", (root / L"isolated-profile").c_str());
        auto frontend = root / L"moonlight-lite-gpui-prototype.exe";
        STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        if (!CreateProcessW(frontend.c_str(), nullptr, nullptr, nullptr, FALSE, 0,
                            nullptr, root.c_str(), &startup, &process)) return 4;
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        return 0;
    }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) return 1;
    auto* window = SDL_CreateWindow("GPUI controls fixture - no host (F10 borderless, F11 exclusive)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1100, 800, SDL_WINDOW_SHOWN);
    if (!window) return 2;
    SDL_SysWMinfo info = {}; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return 3;
    auto* surface = SDL_GetWindowSurface(window);
    if (surface) { SDL_FillRect(surface, nullptr, SDL_MapRGB(surface->format, 20, 65, 105)); SDL_UpdateWindowSurface(window); }
    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line)) {
            SDL_Event e = {}; e.type = SDL_USEREVENT;
            e.user.data1 = new std::string(line);
            if (SDL_PushEvent(&e) != 1) delete static_cast<std::string*>(e.user.data1);
        }
        SDL_Event e = {}; e.type = SDL_QUIT; SDL_PushEvent(&e);
    }).detach();
    event("\"event\":\"ready\",\"qml_loaded\":false");
    event("\"event\":\"stream_started\"");
    bool active = false;
    bool quit = false;
    while (!quit) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) break;
        if (e.type == SDL_QUIT) quit = true;
        if (e.type == SDL_USEREVENT) {
            std::unique_ptr<std::string> line(static_cast<std::string*>(e.user.data1));
            std::cerr << "command " << *line << '\n' << std::flush;
            if (line->find("shutdown") != std::string::npos) quit = true;
            if (line->find("resume") != std::string::npos) {
                SDL_RestoreWindow(window); SDL_RaiseWindow(window); SDL_PumpEvents();
                std::cerr << "resume focus=" << !!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) << '\n' << std::flush;
                active = false; controls(false);
            }
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.sym == SDLK_o && (e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_ALT) && (e.key.keysym.mod & KMOD_SHIFT)) {
                AllowSetForegroundWindow(ASFW_ANY); active = true; controls(true);
            }
            if (e.key.keysym.sym == SDLK_F10 || e.key.keysym.sym == SDLK_F11) {
                const Uint32 mode = e.key.keysym.sym == SDLK_F10 ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
                SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? 0 : mode);
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN && active) {
            const char* direction = nullptr;
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP: direction = "up"; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: direction = "down"; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: direction = "left"; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: direction = "right"; break;
            case SDL_CONTROLLER_BUTTON_A: direction = "select"; break;
            case SDL_CONTROLLER_BUTTON_B: direction = "back"; break;
            case SDL_CONTROLLER_BUTTON_X: direction = "edit"; break;
            }
            if (direction) std::cout << "{\"protocol\":1,\"event\":\"controls_navigate\",\"direction\":\"" << direction << "\"}\n" << std::flush;
        }
        if (e.type == SDL_WINDOWEVENT) std::cerr << "window event=" << int(e.window.event)
            << " active=" << active << " minimized=" << !!(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) << '\n' << std::flush;
    }
    SDL_DestroyWindow(window); SDL_Quit();
}
