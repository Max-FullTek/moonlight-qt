#include "decoderfactory.h"
#ifdef HAVE_FFMPEG
#include "ffmpeg.h"
#endif
#ifdef HAVE_SLVIDEO
#include "slvid.h"
#endif

bool VideoDecoderFactory::choose(IFFmpegDecoderHost* host, VideoDecoderOptions::Decoder vds,
                                 VideoDecoderOptions::Renderer renderer,
                                 SDL_Window* window, int videoFormat, int width, int height,
                                 int frameRate, bool enableVsync, bool enableFramePacing,
                                 bool enableVideoEnhancement, bool ignoreAspectRatio,
                                 bool testOnly, IVideoDecoder*& chosenDecoder)
{
    DECODER_PARAMETERS params;

    // We should never have vsync enabled for test-mode.
    // It introduces unnecessary delay for renderers that may
    // block while waiting for a backbuffer swap.
    SDL_assert(!enableVsync || !testOnly);

    params.width = width;
    params.height = height;
    params.frameRate = frameRate;
    params.videoFormat = videoFormat;
    params.window = window;
    params.enableVsync = enableVsync;
    params.enableFramePacing = enableFramePacing;
    // Preserve the saved preference while making the effective decoder state
    // match the UI: video enhancement is unavailable with software decoding.
    params.enableVideoEnhancement = enableVideoEnhancement &&
                                    vds != VideoDecoderOptions::Software;
    params.ignoreAspectRatio = ignoreAspectRatio;
    params.testOnly = testOnly;
    params.vds = vds;
    params.renderer = renderer;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "V-sync %s",
                enableVsync ? "enabled" : "disabled");

#ifdef HAVE_SLVIDEO
    chosenDecoder = new SLVideoDecoder(testOnly);
    if (chosenDecoder->initialize(&params)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "SLVideo video decoder chosen");
        return true;
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load SLVideo decoder");
        delete chosenDecoder;
        chosenDecoder = nullptr;
    }
#endif

#ifdef HAVE_FFMPEG
    SDL_assert(host != nullptr);
    chosenDecoder = new FFmpegVideoDecoder(testOnly, *host);
    if (chosenDecoder->initialize(&params)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "FFmpeg-based video decoder chosen");
        return true;
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load FFmpeg decoder");
        delete chosenDecoder;
        chosenDecoder = nullptr;
    }
#endif

#if !defined(HAVE_FFMPEG) && !defined(HAVE_SLVIDEO)
#error No video decoding libraries available!
#endif

    // If we reach this, we didn't initialize any decoders successfully
    return false;
}

void VideoDecoderFactory::getInfo(IFFmpegDecoderHost* host, SDL_Window* window,
                             bool& isHardwareAccelerated, bool& isFullScreenOnly,
                             bool& isHdrSupported, VideoResolution& maxResolution)
{
    IVideoDecoder* decoder;

    // Since AV1 support on the host side is in its infancy, let's not consider
    // _only_ a working AV1 decoder to be acceptable and still show the warning
    // dialog indicating lack of hardware decoding support.

    // Try an HEVC Main10 decoder first to see if we have HDR support
    if (choose(host, VideoDecoderOptions::Hardware,
                      VideoDecoderOptions::ProbeOnly,
                      window, VIDEO_FORMAT_H265_MAIN10, 1920, 1080, 60,
                      false, false, false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        isHdrSupported = decoder->isHdrSupported();
        const auto resolution = decoder->getDecoderMaxResolution();
        maxResolution = resolution;
        delete decoder;

        return;
    }

    // Try an AV1 Main10 decoder next to see if we have HDR support
    if (choose(host, VideoDecoderOptions::Hardware,
                      VideoDecoderOptions::ProbeOnly,
                      window, VIDEO_FORMAT_AV1_MAIN10, 1920, 1080, 60,
                      false, false, false, false, true, decoder)) {
        // If we've got a working AV1 Main 10-bit decoder, we'll enable the HDR checkbox
        // but we will still continue probing to get other attributes for HEVC or H.264
        // decoders. See the AV1 comment at the top of the function for more info.
        isHdrSupported = decoder->isHdrSupported();
        delete decoder;
    }
    else {
        // If we found no hardware decoders with HDR, check for a renderer
        // that supports HDR rendering with software decoded frames.
        if (choose(host, VideoDecoderOptions::Software,
                          VideoDecoderOptions::ProbeOnly,
                          window, VIDEO_FORMAT_H265_MAIN10, 1920, 1080, 60,
                          false, false, false, false, true, decoder) ||
            choose(host, VideoDecoderOptions::Software,
                          VideoDecoderOptions::ProbeOnly,
                          window, VIDEO_FORMAT_AV1_MAIN10, 1920, 1080, 60,
                          false, false, false, false, true, decoder)) {
            isHdrSupported = decoder->isHdrSupported();
            delete decoder;
        }
        else {
            // We weren't compiled with an HDR-capable renderer or we don't
            // have the required GPU driver support for any HDR renderers.
            isHdrSupported = false;
        }
    }

    // Try a regular hardware accelerated HEVC decoder now
    if (choose(host, VideoDecoderOptions::Hardware,
                      VideoDecoderOptions::ProbeOnly,
                      window, VIDEO_FORMAT_H265, 1920, 1080, 60,
                      false, false, false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        const auto resolution = decoder->getDecoderMaxResolution();
        maxResolution = resolution;
        delete decoder;

        return;
    }

#if 0 // See AV1 comment at the top of this function
    if (choose(host, VideoDecoderOptions::Hardware,
                      VideoDecoderOptions::ProbeOnly,
                      window, VIDEO_FORMAT_AV1_MAIN8, 1920, 1080, 60,
                      false, false, false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        const auto resolution = decoder->getDecoderMaxResolution();
        maxResolution = resolution;
        delete decoder;

        return;
    }
#endif

    // If we still didn't find a hardware decoder, try H.264 now.
    // This will fall back to software decoding, so it should always work.
    if (choose(host, VideoDecoderOptions::Auto,
                      VideoDecoderOptions::ProbeOnly,
                      window, VIDEO_FORMAT_H264, 1920, 1080, 60,
                      false, false, false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        const auto resolution = decoder->getDecoderMaxResolution();
        maxResolution = resolution;
        delete decoder;

        return;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to find ANY working H.264 or HEVC decoder!");
}

VideoDecoderFactory::Availability
VideoDecoderFactory::getAvailability(IFFmpegDecoderHost* host, SDL_Window* window,
                                VideoDecoderOptions::Decoder vds,
                                int videoFormat, int width, int height, int frameRate)
{
    IVideoDecoder* decoder;

    if (!choose(host, vds,
                       VideoDecoderOptions::ProbeOnly,
                       window, videoFormat, width, height, frameRate,
                       false, false, false, false, true, decoder)) {
        return Availability::None;
    }

    bool hw = decoder->isHardwareAccelerated();

    delete decoder;

    return hw ? Availability::Hardware : Availability::Software;
}

