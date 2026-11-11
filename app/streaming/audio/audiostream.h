#pragma once

#include "audiopipeline.h"
#include "SDL_compat.h"
#include <atomic>

// Owned by one streaming session. Initialize/cleanup/decode run on the
// Limelight audio thread; only the muted flag is changed by the window owner.
class AudioStream
{
public:
    AudioStream() = default;
    ~AudioStream();
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;
    static int capabilities();
    static bool testConfiguration(int audioConfiguration);
    void initialize(const OPUS_MULTISTREAM_CONFIGURATION& config);
    void cleanup();
    void decodeAndPlay(const char* sampleData, int sampleLength);
    void setMuted(bool muted) { m_AudioMuted.store(muted, std::memory_order_relaxed); }
private:
    bool initializeRenderer();
    std::atomic_bool m_AudioMuted{false};
    OpusMSDecoder* m_OpusDecoder = nullptr;
    IAudioRenderer* m_AudioRenderer = nullptr;
    OPUS_MULTISTREAM_CONFIGURATION m_ActiveAudioConfig{};
    OPUS_MULTISTREAM_CONFIGURATION m_OriginalAudioConfig{};
    int m_AudioSampleCount = 0;
    Uint32 m_DropAudioEndTime = 0;
};
