#include "streaming/video/decodebuffer.h"
#include "streaming/video/ffmpeg-renderers/pacer/pacer.h"

// Reuse the exact production probe bitstreams, including 10-bit and 4:4:4.
#include "../../app/streaming/video/ffmpeg_videosamples.cpp"

extern "C" {
#include <libavutil/imgutils.h>
}
#include <h264_stream.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
SDL_AssertState SDLCALL reportAssertion(const SDL_AssertData* data, void*)
{
    std::fprintf(stderr, "SDL assertion: %s (%s:%d)\n", data->condition, data->filename, data->linenum);
    std::_Exit(2);
}
DECODE_UNIT unitFor(std::vector<uint8_t>& bytes, LENTRY (&entries)[3])
{
    int offset = 0;
    for (int i = 0; i < 3; ++i) {
        entries[i] = {};
        entries[i].data = reinterpret_cast<char*>(bytes.data() + offset);
        entries[i].length = i == 2 ? static_cast<int>(bytes.size()) - offset : static_cast<int>(bytes.size() / 3);
        entries[i].bufferType = BUFFER_TYPE_PICDATA;
        entries[i].next = i == 2 ? nullptr : &entries[i + 1];
        offset += entries[i].length;
    }
    DECODE_UNIT unit{};
    unit.fullLength = static_cast<int>(bytes.size());
    unit.bufferList = entries;
    unit.frameType = FRAME_TYPE_IDR;
    return unit;
}
void checkPadding(uint8_t* data, int size)
{
    for (int i = 0; i < AV_INPUT_BUFFER_PADDING_SIZE; ++i) {
        require(data[size + i] == 0, "FFmpeg input padding is zero after the actual payload");
    }
}
AVFrame* decode(AVCodecID codecId, uint8_t* data, int length)
{
    const AVCodec* codec = avcodec_find_decoder(codecId);
    require(codec != nullptr, "pinned FFmpeg supplies software decoder");
    AVCodecContext* context = avcodec_alloc_context3(codec);
    require(context != nullptr, "allocate codec context");
    context->thread_count = 1;
    require(avcodec_open2(context, codec, nullptr) == 0, "open software decoder");
    AVPacket* packet = av_packet_alloc();
    require(packet != nullptr, "allocate packet");
    packet->data = data;
    packet->size = length;
    require(avcodec_send_packet(context, packet) == 0, "send production sample");
    AVFrame* frame = av_frame_alloc();
    require(frame != nullptr, "allocate decoded frame");
    int result = avcodec_receive_frame(context, frame);
    if (result == AVERROR(EAGAIN)) {
        require(avcodec_send_packet(context, nullptr) == 0, "flush delayed test frame");
        result = avcodec_receive_frame(context, frame);
    }
    require(result == 0, "decode production sample");
    av_packet_free(&packet);
    avcodec_free_context(&context);
    return frame;
}
std::vector<uint8_t> pixels(AVFrame* frame)
{
    int size = av_image_get_buffer_size(static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, 1);
    require(size > 0, "decoded pixel format has a packed size");
    std::vector<uint8_t> output(size);
    require(av_image_copy_to_buffer(output.data(), size, frame->data, frame->linesize,
            static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, 1) == size,
            "copy decoded pixels for independent comparison");
    return output;
}
class Renderer : public IFFmpegRenderer {
public:
    Renderer() : IFFmpegRenderer(RendererType::Unknown) {}
    bool initialize(PDECODER_PARAMETERS) override { return true; }
    bool prepareDecoderContext(AVCodecContext*, AVDictionary**) override { return true; }
    bool isRenderThreadSupported() override { return false; }
    void renderFrame(AVFrame* frame) override
    {
        require(frame->width == 1280 && frame->height == 720, "pacer receives decoded 720p frame");
        ++count;
    }
    int count = 0;
};
template<size_t N>
void sample(VideoDecodeBuffer& buffer, Pacer& pacer, AVCodecID codecId, const uint8_t (&input)[N])
{
    std::vector<uint8_t> bytes(input, input + N);
    LENTRY entries[3];
    auto unit = unitFor(bytes, entries);
    const int size = buffer.assemble(&unit, false, codecId == AV_CODEC_ID_AV1);
    checkPadding(buffer.data(), size);
    std::vector<uint8_t> direct(input, input + N);
    direct.resize(N + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    AVFrame* expected = decode(codecId, direct.data(), static_cast<int>(N));
    AVFrame* actual = decode(codecId, buffer.data(), size);
    require(expected->format == actual->format && expected->width == actual->width &&
            expected->height == actual->height && pixels(expected) == pixels(actual),
            "fragmented assembly decodes to exactly the original pixels");
    av_frame_free(&expected);
    actual->pkt_dts = LiGetMicroseconds();
    pacer.submitFrame(actual);
    pacer.renderOnMainThread();
}
}

int main()
{
    SDL_SetMainReady();
    SDL_SetAssertionHandler(reportAssertion, nullptr);
    require(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0, "select dummy video");
    require(SDL_Init(SDL_INIT_VIDEO) == 0, "initialize dummy video");
    SDL_Window* window = SDL_CreateWindow("decode test", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    require(window != nullptr, "create hidden dummy window");
    VideoDecodeBuffer buffer;
    {
        Renderer renderer;
        VIDEO_STATS stats{};
        Pacer pacer(&renderer, &stats);
        require(pacer.initialize(window, 60, false), "initialize production pacer");
        using namespace VideoSamples;
        sample(buffer, pacer, AV_CODEC_ID_H264, k_H264TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_HEVC, k_HEVCMainTestFrame);
        sample(buffer, pacer, AV_CODEC_ID_HEVC, k_HEVCMain10TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_AV1, k_AV1Main8TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_AV1, k_AV1Main10TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_H264, k_h264High_444TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_HEVC, k_HEVCRExt8_444TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_HEVC, k_HEVCRExt10_444TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_AV1, k_AV1High8_444TestFrame);
        sample(buffer, pacer, AV_CODEC_ID_AV1, k_AV1High10_444TestFrame);
        require(stats.renderedFrames == 10 && renderer.count == 10, "all ten decoded samples reach pacer renderer");
    }
    // Larger-than-initial buffer, then smaller packet: no stale payload in padding.
    std::vector<uint8_t> large(2 * 1024 * 1024 + 17, 0xA5);
    LENTRY entries[3];
    auto unit = unitFor(large, entries);
    int size = buffer.assemble(&unit, false, false);
    require(size == static_cast<int>(large.size()) && std::memcmp(buffer.data(), large.data(), size) == 0,
            "grow and concatenate packet without truncation");
    checkPadding(buffer.data(), size);
    uint8_t* storage = buffer.data();
    std::vector<uint8_t> small(137, 0x53);
    unit = unitFor(small, entries);
    unit.frameType = FRAME_TYPE_PFRAME;
    size = buffer.assemble(&unit, false, false);
    require(buffer.data() == storage && size == 137 && std::memcmp(buffer.data(), small.data(), size) == 0,
            "reuse allocated storage for smaller P-frame");
    checkPadding(buffer.data(), size);

    // Split AV1 frame header + tile group is merged with metadata hoisted.
    std::vector<uint8_t> split{0x1A, 2, 0x10, 0x20, 0x2A, 1, 0x30, 0x22, 2, 0x40, 0x50};
    unit = unitFor(split, entries);
    size = buffer.assemble(&unit, false, true);
    const std::vector<uint8_t> merged{0x2A, 1, 0x30, 0x32, 4, 0x10, 0x00, 0x40, 0x50};
    require(size == static_cast<int>(merged.size()) && std::memcmp(buffer.data(), merged.data(), size) == 0,
            "AV1 assembly repacks split frame with metadata");
    checkPadding(buffer.data(), size);

    // Existing one-reference SPS must remain byte-for-byte stable through fixup.
    std::vector<uint8_t> sps(std::begin(VideoSamples::k_H264TestFrame), std::end(VideoSamples::k_H264TestFrame));
    int nalStart = 0, nalEnd = 0;
    require(find_nal_unit(sps.data(), static_cast<int>(sps.size()), &nalStart, &nalEnd) > 0 && nalStart == 4,
            "locate complete sample SPS");
    sps.resize(nalEnd);
    LENTRY spsEntry{};
    spsEntry.data = reinterpret_cast<char*>(sps.data());
    spsEntry.length = static_cast<int>(sps.size());
    spsEntry.bufferType = BUFFER_TYPE_SPS;
    unit = {};
    unit.fullLength = spsEntry.length;
    unit.frameType = FRAME_TYPE_IDR;
    unit.bufferList = &spsEntry;
    size = buffer.assemble(&unit, true, false);
    require(size == static_cast<int>(sps.size()) && std::memcmp(buffer.data(), sps.data(), size) == 0,
            "already compatible SPS remains unchanged");
    checkPadding(buffer.data(), size);
    h264_stream_t* stream = h264_new();
    require(stream != nullptr, "allocate SPS parser");
    require(read_nal_unit(stream, sps.data() + 4, static_cast<int>(sps.size()) - 4) > 0, "parse sample SPS");
    stream->sps->num_ref_frames = 4;
    stream->sps->vui.max_dec_frame_buffering = 4;
    stream->sps->vui.bitstream_restriction_flag = 0;
    std::vector<uint8_t> incompatible(256, 0);
    const int written = write_nal_unit(stream, incompatible.data() + 3, 253);
    require(written > 1, "build SPS requiring fixup");
    incompatible[3] = 1;
    incompatible.resize(static_cast<size_t>(written) + 3);
    spsEntry.data = reinterpret_cast<char*>(incompatible.data());
    spsEntry.length = static_cast<int>(incompatible.size());
    unit.fullLength = spsEntry.length;
    size = buffer.assemble(&unit, true, false);
    require(read_nal_unit(stream, buffer.data() + 4, size - 4) > 0, "parse fixed SPS");
    require(stream->sps->num_ref_frames == 1 && stream->sps->vui.max_dec_frame_buffering == 1 &&
            stream->sps->vui.bitstream_restriction_flag == 1 && stream->sps->vui.num_reorder_frames == 0,
            "fixup restores single-reference low-latency SPS restrictions");
    h264_free(stream);
    checkPadding(buffer.data(), size);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("video_decode_path=passed (production samples -> assembly -> actual FFmpeg software decode -> pacer)");
}
