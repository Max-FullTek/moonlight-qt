#include "streaming/network/bandwidth.h"

#include <windows.h>
#include <SDL.h>
#include <string_view>

class BandwidthCalculatorTest
{
public:
    static int run(bool sdlPump)
    {
        auto* calculator = BandwidthCalculator::instance();
        calculator->addBytes(100);
        if (calculator->m_bytesReceived != 0) return 1;
        calculator->start();
        if (!calculator->m_running || !calculator->m_updateTimer) return 2;
        const auto timer = calculator->m_updateTimer;
        const auto started = calculator->m_lastUpdate;
        calculator->start();
        if (calculator->m_updateTimer != timer) return 3;

        MSG message{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (!PeekMessageW(&message, nullptr, WM_TIMER, WM_TIMER, PM_REMOVE)) {
            if (std::chrono::steady_clock::now() >= deadline) return 4;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_TIMER);
        }
        DispatchMessageW(&message);
        if (calculator->m_lastUpdate <= started) return 5;
        if (sdlPump) {
            const auto beforeSdlPump = calculator->m_lastUpdate;
            const auto sdlDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (calculator->m_lastUpdate == beforeSdlPump) {
                if (std::chrono::steady_clock::now() >= sdlDeadline) return 9;
                SDL_PumpEvents();
                SDL_Delay(1);
            }
        }

        calculator->stop();
        if (calculator->m_running || calculator->m_updateTimer) return 6;
        const auto sampled = calculator->m_lastUpdate;
        const auto received = calculator->m_bytesReceived.load();
        calculator->addBytes(100);
        DispatchMessageW(&message); // A WM_TIMER already queued before KillTimer.
        if (calculator->m_lastUpdate != sampled || calculator->m_bytesReceived != received) return 7;
        calculator->stop();

        calculator->start();
        if (!calculator->m_running || !calculator->m_updateTimer ||
            calculator->getCurrentBandwidthKbps() != 0 || calculator->m_bytesReceived != 0) return 8;
        delete calculator; // Destruction also cancels an active timer.
        BandwidthCalculator::s_instance = nullptr;
        DispatchMessageW(&message);
        return 0;
    }
};

int main(int argc, char** argv)
{
    const bool sdlPump = argc == 2 && std::string_view(argv[1]) == "--sdl-pump";
    if (argc != 1 && !sdlPump) return 10;
    if (sdlPump && SDL_Init(SDL_INIT_VIDEO) != 0) return 11;
    const int result = BandwidthCalculatorTest::run(sdlPump);
    if (sdlPump) SDL_Quit();
    return result;
}
