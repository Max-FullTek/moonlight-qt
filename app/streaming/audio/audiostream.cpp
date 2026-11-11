#include "audiostream.h"
#include "audiopipeline.h"

namespace {
IAudioRenderer* createRenderer(const OPUS_MULTISTREAM_CONFIGURATION& config)
{
    return AudioPipeline::createRenderer(AudioPipeline::backendFromEnvironment(), config);
}
}

AudioStream::~AudioStream()
{
    cleanup();
}

void AudioStream::initialize(const OPUS_MULTISTREAM_CONFIGURATION& config)
{
    m_OriginalAudioConfig = config;
    initializeRenderer();
}

void AudioStream::cleanup()
{
    delete m_AudioRenderer;
    m_AudioRenderer = nullptr;
    opus_multistream_decoder_destroy(m_OpusDecoder);
    m_OpusDecoder = nullptr;
}

int AudioStream::capabilities()
{
    int caps = CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION;
#ifdef STEAM_LINK
    caps |= CAPABILITY_SLOW_OPUS_DECODER;
#endif
    return caps;
}

bool AudioStream::testConfiguration(int audioConfiguration)
{
    OPUS_MULTISTREAM_CONFIGURATION config{};
    config.sampleRate = 48000;
    config.samplesPerFrame = 240;
    config.channelCount = CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(audioConfiguration);
    IAudioRenderer* renderer = createRenderer(config);
    if (renderer == nullptr) return false;
    delete renderer;
    return true;
}

bool AudioStream::initializeRenderer()
{
    int error;

    SDL_assert(m_OriginalAudioConfig.channelCount > 0);
    SDL_assert(m_AudioRenderer == nullptr);
    SDL_assert(m_OpusDecoder == nullptr);

    m_AudioRenderer = createRenderer(m_OriginalAudioConfig);

    // We may be unable to create an audio renderer right now
    if (m_AudioRenderer == nullptr) {
        return false;
    }

    // Allow the chosen renderer to remap Opus channels as needed to ensure proper output
    m_ActiveAudioConfig = m_OriginalAudioConfig;
    m_AudioRenderer->remapChannels(&m_ActiveAudioConfig);

    // Create the Opus decoder with the renderer's preferred channel mapping
    m_OpusDecoder = AudioPipeline::createDecoder(m_ActiveAudioConfig, &error);
    if (m_OpusDecoder == nullptr) {
        delete m_AudioRenderer;
        m_AudioRenderer = nullptr;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create decoder: %d",
                     error);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Audio stream has %d channels",
                m_ActiveAudioConfig.channelCount);
    return true;
}

void AudioStream::decodeAndPlay(const char* sampleData, int sampleLength)
{
#ifndef STEAM_LINK
    // Set this thread to high priority to reduce the chance of missing
    // our sample delivery time. On Steam Link, this causes starvation
    // of other threads due to severely restricted CPU time available,
    // so we will skip it on that platform.
    if (m_AudioSampleCount == 0) {
        if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to set audio thread to high priority: %s",
                        SDL_GetError());
        }
    }
#endif

    // See if we need to drop this sample
    if (m_DropAudioEndTime != 0) {
        if (SDL_TICKS_PASSED(SDL_GetTicks(), m_DropAudioEndTime)) {
            // Avoid calling SDL_GetTicks() now
            m_DropAudioEndTime = 0;

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Audio drop window has ended");
        }
        else {
            // We're still in the drop window
            return;
        }
    }

    m_AudioSampleCount++;

    // If audio is muted, don't decode or play the audio
    if (m_AudioMuted.load(std::memory_order_relaxed)) {
        return;
    }

    if (m_AudioRenderer != nullptr) {
        if (!AudioPipeline::decodeAndSubmit(m_OpusDecoder,
                                            *m_AudioRenderer,
                                            m_ActiveAudioConfig,
                                            sampleData, sampleLength)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Reinitializing audio renderer after failure");

            opus_multistream_decoder_destroy(m_OpusDecoder);
            m_OpusDecoder = nullptr;

            delete m_AudioRenderer;
            m_AudioRenderer = nullptr;
        }
    }

    // Only try to recreate the audio renderer every 200 samples (1 second)
    // to avoid thrashing if the audio device is unavailable. It is
    // safe to reinitialize here because we can't be torn down while
    // the audio decoder/playback thread is still alive.
    if (m_AudioRenderer == nullptr && (m_AudioSampleCount % 200) == 0) {
        // Since we're doing this inline and audio initialization takes time, we need
        // to drop samples to account for the time we've spent blocking audio rendering
        // so we return to real-time playback and don't accumulate latency.
        Uint32 audioReinitStartTime = SDL_GetTicks();
        if (initializeRenderer()) {
            Uint32 audioReinitStopTime = SDL_GetTicks();

            m_DropAudioEndTime = audioReinitStopTime + (audioReinitStopTime - audioReinitStartTime);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Audio reinitialization took %d ms - starting drop window",
                        audioReinitStopTime - audioReinitStartTime);
        }
    }
}
