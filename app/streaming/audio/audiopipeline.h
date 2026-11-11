#pragma once

#include "renderers/renderer.h"
#include <opus_multistream.h>

namespace AudioPipeline {
enum class Backend { Automatic, Sdl, SlAudio, Unknown };

// ML_AUDIO accepts empty (automatic), sdl, and the built-in slaudio backend,
// case-insensitively; unknown names retain the renderer-failure path.
Backend backendFromEnvironment();

// Caller owns the returned renderer.
IAudioRenderer* createRenderer(Backend backend, const OPUS_MULTISTREAM_CONFIGURATION& config);

OpusMSDecoder* createDecoder(const OPUS_MULTISTREAM_CONFIGURATION& config, int* error);

// Decode directly into the renderer's buffer. False retains the existing
// renderer failure/reinitialization signal; a missing buffer skips this packet.
bool decodeAndSubmit(OpusMSDecoder* decoder, IAudioRenderer& renderer,
                     const OPUS_MULTISTREAM_CONFIGURATION& config,
                     const char* sampleData, int sampleLength);
} // namespace AudioPipeline
