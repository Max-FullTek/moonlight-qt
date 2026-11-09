#pragma once

#include "renderers/renderer.h"
#include <opus_multistream.h>

namespace AudioPipeline {
enum class Backend { Automatic, Sdl, SlAudio, Unknown };

// Caller owns the returned renderer. Backend parsing remains at the caller so
// the existing environment-variable interpretation is preserved.
IAudioRenderer* createRenderer(Backend backend, const OPUS_MULTISTREAM_CONFIGURATION& config);

OpusMSDecoder* createDecoder(const OPUS_MULTISTREAM_CONFIGURATION& config, int* error);

// Decode directly into the renderer's buffer. False retains the existing
// renderer failure/reinitialization signal; a missing buffer skips this packet.
bool decodeAndSubmit(OpusMSDecoder* decoder, IAudioRenderer& renderer,
                     const OPUS_MULTISTREAM_CONFIGURATION& config,
                     const char* sampleData, int sampleLength);
} // namespace AudioPipeline
