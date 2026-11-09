#include "streaming/audio/renderers/sdl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <atomic>

namespace {
std::atomic_bool audioError{false};

void SDLCALL reportLog(void*, int, SDL_LogPriority priority, const char* message)
{
    std::fprintf(stderr, "%s\n", message);
    if (priority >= SDL_LOG_PRIORITY_ERROR) {
        audioError.store(true, std::memory_order_relaxed);
    }
}

SDL_AssertState SDLCALL reportAssertion(const SDL_AssertData* data, void*)
{
    std::fprintf(stderr, "SDL assertion: %s (%s:%d)\n",
                 data->condition, data->filename, data->linenum);
    std::_Exit(2);
}

bool check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s; SDL: %s\n", message, SDL_GetError());
    }
    return condition;
}

bool playSilence(int channels, int samplesPerFrame)
{
    OPUS_MULTISTREAM_CONFIGURATION config{};
    config.sampleRate = 48000;
    config.channelCount = channels;
    config.samplesPerFrame = samplesPerFrame;

    SdlAudioRenderer renderer;
    if (!check(renderer.prepareForPlayback(&config), "open dummy audio device")) {
        return false;
    }
    const char* driver = SDL_GetCurrentAudioDriver();
    if (!check(driver && std::strcmp(driver, "dummy") == 0,
               "test must never use physical audio hardware")) {
        return false;
    }
    if (!check(renderer.getAudioBufferFormat() == IAudioRenderer::AudioFormat::Float32NE &&
               renderer.getAudioBufferSampleSize() == sizeof(float),
               "renderer must preserve float PCM format")) {
        return false;
    }
    void* buffer = renderer.getAudioBuffer(nullptr);
    if (!check(buffer != nullptr, "allocate PCM frame")) {
        return false;
    }
    const int frameBytes = channels * samplesPerFrame * sizeof(float);
    std::memset(buffer, 0, frameBytes);
    if (!check(renderer.submitAudio(0), "accept empty frame")) {
        return false;
    }
    // Real SDL queue submission and the actual common-c pending-duration API.
    // No stream is started, so the common-c pending queue remains empty.
    for (int frame = 0; frame < 4; ++frame) {
        if (!check(renderer.submitAudio(frameBytes), "queue PCM frame")) {
            return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    SDL_SetMainReady();
    SDL_SetAssertionHandler(reportAssertion, nullptr);
    SDL_LogSetOutputFunction(reportLog, nullptr);
    if (!check(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0,
               "select isolated dummy audio driver")) {
        return 1;
    }
    // Reopen after every destructor to detect leaked SDL audio initialization
    // and devices. Cover stereo, 5.1 and 7.1 at both normal packet durations.
    for (int channels : {2, 6, 8}) {
        for (int samples : {240, 480}) {
            if (!playSilence(channels, samples) ||
                !check(SDL_WasInit(SDL_INIT_AUDIO) == 0, "release audio subsystem") ||
                !check(!audioError.load(std::memory_order_relaxed), "SDL audio reported an error")) {
                return 1;
            }
        }
    }
    std::puts("sdl_audio_renderer=passed (dummy driver, no host or physical audio)");
    return 0;
}
