#pragma once

#include <functional>
#include <deque>
#include "decodebuffer.h"
#include <set>

#include "decoder.h"
#include "ffmpeg-renderers/renderer.h"
#include "ffmpeg-renderers/pacer/pacer.h"
#include "streaming/video/videoenhancement.h"
#include "overlaymanager.h"
#include "ffmpeg-renderers/sdlvid.h"
#ifdef _WIN32
#include "ffmpeg-renderers/d3d11va.h"
#include "ffmpeg-renderers/dxva2.h"
#endif
#ifdef HAVE_LIBPLACEBO_VULKAN
#include "ffmpeg-renderers/plvk.h"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}

// The existing owner supplies live overlays, resources and environment policy.
// This boundary carries no frames and must outlive the decoder.
class IFFmpegDecoderHost : public ISdlRendererHost
#ifdef _WIN32
    , public ID3D11RendererHost, public IDxva2RendererHost
#endif
#ifdef HAVE_LIBPLACEBO_VULKAN
    , public IPlVkRendererHost
#endif
{
public:
    virtual Overlay::OverlayManager& getOverlayManager() = 0;
    virtual int getCurrentBandwidthKbps() = 0;
    virtual bool isGpuSlow() = 0;
};

class FFmpegVideoDecoder : public IVideoDecoder {
public:
    FFmpegVideoDecoder(bool testOnly, IFFmpegDecoderHost& host);
    virtual ~FFmpegVideoDecoder() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool isHardwareAccelerated() override;
    virtual bool isAlwaysFullScreen() override;
    virtual bool isHdrSupported() override;
    virtual int getDecoderCapabilities() override;
    virtual int getDecoderColorspace() override;
    virtual int getDecoderColorRange() override;
    virtual VideoResolution getDecoderMaxResolution() override;
    virtual int submitDecodeUnit(PDECODE_UNIT du) override;
    virtual void renderFrameOnMainThread() override;
    virtual void setHdrMode(bool enabled) override;
    virtual bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info) override;

    virtual IFFmpegRenderer* getBackendRenderer();

private:
    enum class TestMode {
        // No test frame and prepare for rendering
        NoTesting,

        // Submit only the test frame and do not prepare for rendering
        TestFrameOnly,

        // Submit the test frame and prepare for rendering
        TestFrame
    };

    bool completeInitialization(const AVCodec* decoder,
                                enum AVPixelFormat requiredFormat,
                                PDECODER_PARAMETERS params,
                                TestMode testMode,
                                bool useAlternateFrontend);

    void stringifyVideoStats(VIDEO_STATS& stats, char* output, int length);
    const char* dynamicRangeLabel();

    void logVideoStats(VIDEO_STATS& stats, const char* title);

    void addVideoStats(VIDEO_STATS& src, VIDEO_STATS& dst);

    bool createFrontendRenderer(PDECODER_PARAMETERS params, bool useAlternateFrontend);

    static
    bool isDecoderMatchForParams(const AVCodec *decoder, PDECODER_PARAMETERS params);

    static
    bool isZeroCopyFormat(AVPixelFormat format);

    static
    int getAVCodecCapabilities(const AVCodec *codec);

    bool tryInitializeHwAccelDecoder(PDECODER_PARAMETERS params,
                                     int pass,
                                     std::set<const AVCodec*>& terminallyFailedHardwareDecoders);

    bool tryInitializeNonHwAccelDecoder(PDECODER_PARAMETERS params,
                                        bool requireZeroCopyFormat,
                                        std::set<const AVCodec*>& terminallyFailedHardwareDecoders);

    bool tryInitializeRendererForUnknownDecoder(const AVCodec* decoder,
                                                PDECODER_PARAMETERS params,
                                                bool tryHwAccel);

    bool tryInitializeRenderer(const AVCodec* decoder,
                               enum AVPixelFormat requiredFormat,
                               PDECODER_PARAMETERS params,
                               const AVCodecHWConfig* hwConfig,
                               IFFmpegRenderer::InitFailureReason* failureReason,
                               std::function<IFFmpegRenderer*()> createRendererFunc);

    IFFmpegRenderer* createHwAccelRenderer(const AVCodecHWConfig* hwDecodeCfg, PDECODER_PARAMETERS params, int pass);

    bool initializeRendererInternal(IFFmpegRenderer* renderer, PDECODER_PARAMETERS params);

    static bool isSeparateTestDecoderRequired(const AVCodec* decoder);

    void reset();

    static
    enum AVPixelFormat ffGetFormat(AVCodecContext* context,
                                   const enum AVPixelFormat* pixFmts);

    void decoderThreadProc();

    static int decoderThreadProcThunk(void* context);

    IFFmpegDecoderHost& m_Host;
    AVPacket* m_Pkt;
    AVCodecContext* m_VideoDecoderCtx;
    enum AVPixelFormat m_RequiredPixelFormat;
    VideoDecodeBuffer m_DecodeBuffer;
    const AVCodecHWConfig* m_HwDecodeCfg;
    IFFmpegRenderer* m_BackendRenderer;
    IFFmpegRenderer* m_FrontendRenderer;
    int m_ConsecutiveFailedDecodes;
    Pacer* m_Pacer;
    VIDEO_STATS m_ActiveWndVideoStats;
    VIDEO_STATS m_LastWndVideoStats;
    VIDEO_STATS m_GlobalVideoStats;
    std::set<IFFmpegRenderer::RendererType> m_FailedRenderers;

    int m_FramesIn;
    int m_FramesOut;

    int m_LastFrameNumber;
    int m_StreamFps;
    int m_OriginalVideoWidth;
    int m_OriginalVideoHeight;
    int m_VideoFormat;
    bool m_NeedsSpsFixup;
    bool m_NeedsAv1ObuRepack;
    bool m_LoggedHdr10PlusMetadata;
    bool m_TestOnly;
    TestMode m_CurrentTestMode;
    SDL_Thread* m_DecoderThread;
    SDL_atomic_t m_DecoderThreadShouldQuit;
    VideoEnhancement* m_VideoEnhancement;

    // Data buffers in the queued DU are not valid
    std::deque<DECODE_UNIT> m_FrameInfoQueue;

};
