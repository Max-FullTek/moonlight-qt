#pragma once

#include "decoder.h"

class IFFmpegDecoderHost;

// Production decoder selection and capability probing shared by native owners.
namespace VideoDecoderFactory {
enum class Availability { None, Software, Hardware };

bool choose(IFFmpegDecoderHost* host, VideoDecoderOptions::Decoder selection,
            VideoDecoderOptions::Renderer renderer, SDL_Window* window,
            int videoFormat, int width, int height, int frameRate,
            bool enableVsync, bool enableFramePacing, bool enableVideoEnhancement,
            bool ignoreAspectRatio, bool testOnly, IVideoDecoder*& chosenDecoder);
void getInfo(IFFmpegDecoderHost* host, SDL_Window* window,
             bool& isHardwareAccelerated, bool& isFullScreenOnly,
             bool& isHdrSupported, VideoResolution& maxResolution);
Availability getAvailability(IFFmpegDecoderHost* host, SDL_Window* window,
                             VideoDecoderOptions::Decoder selection,
                             int videoFormat, int width, int height, int frameRate);
}
