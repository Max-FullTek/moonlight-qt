#include "audiopipeline.h"
#include "renderers/sdl.h"
#ifdef HAVE_SLAUDIO
#include "renderers/slaud.h"
#endif

namespace AudioPipeline {
namespace {
template <typename Renderer>
IAudioRenderer* tryRenderer(const OPUS_MULTISTREAM_CONFIGURATION& config)
{
    auto* renderer = new Renderer();
    if (renderer->prepareForPlayback(&config)) {
        return renderer;
    }
    delete renderer;
    return nullptr;
}
}

IAudioRenderer* createRenderer(Backend backend, const OPUS_MULTISTREAM_CONFIGURATION& config)
{
    switch (backend) {
    case Backend::Sdl:
        return tryRenderer<SdlAudioRenderer>(config);
#ifdef HAVE_SLAUDIO
    case Backend::SlAudio:
        return tryRenderer<SLAudioRenderer>(config);
#endif
    case Backend::Automatic:
#ifdef HAVE_SLAUDIO
        if (auto* renderer = tryRenderer<SLAudioRenderer>(config)) {
            return renderer;
        }
#endif
        return tryRenderer<SdlAudioRenderer>(config);
    default:
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown audio backend: %s",
                     SDL_getenv("ML_AUDIO"));
        return nullptr;
    }
}

OpusMSDecoder* createDecoder(const OPUS_MULTISTREAM_CONFIGURATION& config, int* error)
{
    return opus_multistream_decoder_create(config.sampleRate, config.channelCount,
                                          config.streams, config.coupledStreams,
                                          config.mapping, error);
}

bool decodeAndSubmit(OpusMSDecoder* decoder, IAudioRenderer& renderer,
                     const OPUS_MULTISTREAM_CONFIGURATION& config,
                     const char* sampleData, int sampleLength)
{
    const int frameSize = renderer.getAudioBufferSampleSize() * config.channelCount;
    int desiredBufferSize = frameSize * config.samplesPerFrame;
    void* buffer = renderer.getAudioBuffer(&desiredBufferSize);
    if (buffer == nullptr) {
        return true;
    }
    int samplesDecoded;
    if (renderer.getAudioBufferFormat() == IAudioRenderer::AudioFormat::Float32NE) {
        samplesDecoded = opus_multistream_decode_float(
            decoder, reinterpret_cast<const unsigned char*>(sampleData), sampleLength,
            static_cast<float*>(buffer), desiredBufferSize / frameSize, 0);
    }
    else {
        samplesDecoded = opus_multistream_decode(
            decoder, reinterpret_cast<const unsigned char*>(sampleData), sampleLength,
            static_cast<short*>(buffer), desiredBufferSize / frameSize, 0);
    }
    if (samplesDecoded > 0) {
        SDL_assert(desiredBufferSize >= frameSize * samplesDecoded);
        desiredBufferSize = frameSize * samplesDecoded;
    }
    else {
        desiredBufferSize = 0;
    }
    return renderer.submitAudio(desiredBufferSize);
}
} // namespace AudioPipeline
