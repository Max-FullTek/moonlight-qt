#include "streaming/audio/audiopipeline.h"
#include "SDL_compat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
int errors = 0;
void SDLCALL logMessage(void*, int, SDL_LogPriority priority, const char* text)
{
    std::fprintf(stderr, "%s\n", text);
    if (priority >= SDL_LOG_PRIORITY_ERROR) ++errors;
}
SDL_AssertState SDLCALL assertion(const SDL_AssertData* data, void*)
{
    std::fprintf(stderr, "SDL assertion: %s\n", data->condition);
    std::_Exit(2);
}
void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

class CaptureRenderer : public IAudioRenderer
{
public:
    explicit CaptureRenderer(AudioFormat format, int sampleCapacity)
        : format(format), floats(sampleCapacity), integers(sampleCapacity) {}
    bool prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION*) override { return true; }
    AudioFormat getAudioBufferFormat() override { return format; }
    void* getAudioBuffer(int* size) override {
        requestedBytes = *size;
        if (skipBuffer) return nullptr;
        *size = std::min(*size, static_cast<int>(floats.size()) * getAudioBufferSampleSize());
        return format == AudioFormat::Float32NE
                ? static_cast<void*>(floats.data()) : static_cast<void*>(integers.data());
    }
    bool submitAudio(int size) override { submittedBytes = size; ++submissions; return submitResult; }
    AudioFormat format;
    std::vector<float> floats;
    std::vector<short> integers;
    int requestedBytes = -1;
    int submittedBytes = -1;
    int submissions = 0;
    bool skipBuffer = false;
    bool submitResult = true;
};

void decodeRoundTrip(int channels, int samples, IAudioRenderer::AudioFormat format)
{
    OPUS_MULTISTREAM_CONFIGURATION config{};
    config.sampleRate = 48000;
    config.channelCount = channels;
    config.samplesPerFrame = samples;
    int error;
    auto* encoder = opus_multistream_surround_encoder_create(
        config.sampleRate, channels, channels == 2 ? 0 : 1,
        &config.streams, &config.coupledStreams, config.mapping, OPUS_APPLICATION_AUDIO, &error);
    require(encoder && error == OPUS_OK, "create real multichannel encoder");
    std::vector<float> pcm(channels * samples);
    for (int i = 0; i < samples; ++i) {
        for (int channel = 0; channel < channels; ++channel) {
            pcm[i * channels + channel] = 0.1f * std::sin((i + channel * 11) * 0.04f);
        }
    }
    std::vector<unsigned char> packet(16384);
    const int bytes = opus_multistream_encode_float(encoder, pcm.data(), samples,
                                                   packet.data(), static_cast<int>(packet.size()));
    require(bytes > 0, "encode Opus packet");
    opus_multistream_encoder_destroy(encoder);
    auto* decoder = AudioPipeline::createDecoder(config, &error);
    require(decoder && error == OPUS_OK, "create production decoder");
    auto* reference = opus_multistream_decoder_create(config.sampleRate, channels, config.streams,
                                                     config.coupledStreams, config.mapping, &error);
    require(reference && error == OPUS_OK, "create independent reference decoder");
    CaptureRenderer renderer(format, channels * samples);
    require(AudioPipeline::decodeAndSubmit(decoder, renderer, config,
            reinterpret_cast<const char*>(packet.data()), bytes), "submit decoded packet");
    require(renderer.requestedBytes == channels * samples * renderer.getAudioBufferSampleSize() &&
            renderer.submittedBytes == renderer.requestedBytes && renderer.submissions == 1,
            "PCM submission size");
    int decoded;
    if (format == IAudioRenderer::AudioFormat::Float32NE) {
        std::vector<float> expected(channels * samples);
        decoded = opus_multistream_decode_float(reference, packet.data(), bytes, expected.data(), samples, 0);
        require(decoded == samples && expected == renderer.floats, "float PCM matches direct Opus output");
    }
    else {
        std::vector<short> expected(channels * samples);
        decoded = opus_multistream_decode(reference, packet.data(), bytes, expected.data(), samples, 0);
        require(decoded == samples && expected == renderer.integers, "integer PCM matches direct Opus output");
    }
    // A renderer may offer a shorter buffer than the configured packet size.
    auto largerConfig = config;
    largerConfig.samplesPerFrame *= 2;
    require(AudioPipeline::decodeAndSubmit(decoder, renderer, largerConfig,
            reinterpret_cast<const char*>(packet.data()), bytes) &&
            renderer.submittedBytes == channels * samples * renderer.getAudioBufferSampleSize(),
            "renderer controls decode capacity and actual decoded size controls submission");
    renderer.submitResult = false;
    require(!AudioPipeline::decodeAndSubmit(decoder, renderer, config,
            reinterpret_cast<const char*>(packet.data()), bytes), "propagate output failure");
    renderer.submitResult = true;
    const int submissions = renderer.submissions;
    renderer.skipBuffer = true;
    require(AudioPipeline::decodeAndSubmit(decoder, renderer, config,
            reinterpret_cast<const char*>(packet.data()), bytes) && renderer.submissions == submissions,
            "unavailable buffer skips submission");
    renderer.skipBuffer = false;
    require(AudioPipeline::decodeAndSubmit(decoder, renderer, config,
            reinterpret_cast<const char*>(packet.data()), -1) && renderer.submittedBytes == 0,
            "invalid packet submits zero PCM bytes");
    opus_multistream_decoder_destroy(reference);
    opus_multistream_decoder_destroy(decoder);
}
}

int main()
{
    SDL_SetMainReady();
    SDL_SetAssertionHandler(assertion, nullptr);
    SDL_LogSetOutputFunction(logMessage, nullptr);
    require(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0, "isolate audio device");
    for (int channels : {2, 6, 8}) {
        for (int samples : {240, 480}) {
            decodeRoundTrip(channels, samples, IAudioRenderer::AudioFormat::Float32NE);
            decodeRoundTrip(channels, samples, IAudioRenderer::AudioFormat::Sint16NE);
        }
    }
    OPUS_MULTISTREAM_CONFIGURATION config{};
    config.sampleRate = 48000;
    config.channelCount = 2;
    config.samplesPerFrame = 240;
    for (auto backend : {AudioPipeline::Backend::Automatic, AudioPipeline::Backend::Sdl}) {
        auto* renderer = AudioPipeline::createRenderer(backend, config);
        require(renderer != nullptr && SDL_strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0,
                "create selected backend on dummy driver");
        delete renderer;
        require(SDL_WasInit(SDL_INIT_AUDIO) == 0, "destroy selected backend");
    }
    require(errors == 0, "no unexpected decoder/backend errors");
    require(SDL_setenv("ML_AUDIO", "test-unsupported", 1) == 0, "isolate backend error text");
    require(AudioPipeline::createRenderer(AudioPipeline::Backend::Unknown, config) == nullptr &&
            AudioPipeline::createRenderer(AudioPipeline::Backend::SlAudio, config) == nullptr,
            "reject unknown or unsupported explicit backend");
    config.sampleRate = -48000;
    require(AudioPipeline::createRenderer(AudioPipeline::Backend::Sdl, config) == nullptr &&
            SDL_WasInit(SDL_INIT_AUDIO) == 0, "failed explicit backend releases audio subsystem");
    require(errors == 3, "only three expected backend errors");
    std::puts("audio_pipeline=passed (real Opus, dummy audio, no host)");
}
