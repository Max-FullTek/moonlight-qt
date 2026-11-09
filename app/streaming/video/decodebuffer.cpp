#include "decodebuffer.h"
#include "av1obu.h"
#include "SDL_compat.h"
#include <cstring>
#include <h264_stream.h>
extern "C" {
#include <libavcodec/defs.h>
}

// Preserve the qmake debug SPS round-trip check in both build systems.
#if defined(QT_DEBUG) || (!defined(QT_NO_DEBUG) && !defined(NDEBUG))
#define VIDEO_BUFFER_DEBUG
#endif

#define MAX_SPS_EXTRA_SIZE 16

VideoDecodeBuffer::VideoDecodeBuffer() :
    m_Data(new uint8_t[1024 * 1024]{}), m_Capacity(1024 * 1024)
{
}

int VideoDecodeBuffer::assemble(PDECODE_UNIT unit, bool fixSps, bool repackAv1)
{
    const size_t required = static_cast<size_t>(unit->fullLength) +
        (unit->frameType == FRAME_TYPE_IDR ? MAX_SPS_EXTRA_SIZE : 0) +
        AV_INPUT_BUFFER_PADDING_SIZE;
    if (required > m_Capacity) {
        // Every packet is rebuilt, so there is no old payload to copy on growth.
        m_Data.reset(new uint8_t[required]);
        m_Capacity = required;
    }
    int offset = 0;
    for (PLENTRY entry = unit->bufferList; entry != nullptr; entry = entry->next) {
        writeEntry(entry, offset, fixSps);
    }
    if (repackAv1) {
        offset = repackAv1TemporalUnit(m_Data.get(), offset);
    }
    // FFmpeg requires zero padding even when reusing a larger previous packet.
    std::memset(m_Data.get() + offset, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    return offset;
}

void VideoDecodeBuffer::writeEntry(PLENTRY entry, int& offset, bool fixSps)
{
    if (fixSps && entry->bufferType == BUFFER_TYPE_SPS) {
        h264_stream_t* stream = h264_new();
        int nalStart, nalEnd;
        bool needsFixup;

        // Read the old NALU
        find_nal_unit((uint8_t*)entry->data, entry->length, &nalStart, &nalEnd);
        read_nal_unit(stream,
                      (unsigned char *)&entry->data[nalStart],
                      nalEnd - nalStart);

        SDL_assert(nalStart == 3 || nalStart == 4); // 3 or 4 byte Annex B start sequence
        SDL_assert(nalEnd == entry->length);

        // Fixup the SPS to what OS X needs to use hardware acceleration
        // This is also critical for decoding latency on the Pi 2.
        needsFixup = (stream->sps->num_ref_frames != 1 || stream->sps->vui.max_dec_frame_buffering != 1);
#ifndef VIDEO_BUFFER_DEBUG
        if (needsFixup)
#endif
        {
            stream->sps->num_ref_frames = 1;
            stream->sps->vui.max_dec_frame_buffering = 1;

            // NVENC doesn't seem to add bitstream restrictions anymore (591.59),
            // so we need to add them ourselves if not present to ensure that
            // the max_dec_frame_buffering option actually takes effect.
            // We use the defaults for everything except max_dec_frame_buffering.
            if (!stream->sps->vui.bitstream_restriction_flag) {
                stream->sps->vui.bitstream_restriction_flag = 1;
                stream->sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
                stream->sps->vui.max_bytes_per_pic_denom = 2;
                stream->sps->vui.max_bits_per_mb_denom = 1;
                stream->sps->vui.log2_max_mv_length_horizontal = 16;
                stream->sps->vui.log2_max_mv_length_vertical = 16;
                stream->sps->vui.num_reorder_frames = 0;
            }

            int initialOffset = offset;

            // Copy the modified NALU data. This clobbers byte 0 and starts NALU data at byte 1.
            // Since it prepended one extra byte, subtract one from the returned length.
            offset += write_nal_unit(stream, (uint8_t*)&m_Data.get()[initialOffset + nalStart - 1],
                                     MAX_SPS_EXTRA_SIZE + entry->length - nalStart) - 1;

            // Copy the NALU prefix over from the original SPS
            memcpy(&m_Data.get()[initialOffset], entry->data, nalStart);
            offset += nalStart;

#ifdef VIDEO_BUFFER_DEBUG
            // If we didn't need a fixup, the SPS should have stayed the exact same
            if (!needsFixup) {
                SDL_assert(offset - initialOffset == entry->length);
                SDL_assert(memcmp(&m_Data.get()[initialOffset], entry->data, entry->length) == 0);
            }
            else {
                // The SPS should never get smaller with a fixup
                SDL_assert(offset - initialOffset >= entry->length);
            }
#endif
        }
#ifndef VIDEO_BUFFER_DEBUG
        else {
            // Write the SPS as-is if it required no modification
            memcpy(&m_Data.get()[offset],
                   entry->data,
                   entry->length);
            offset += entry->length;
        }
#endif

        h264_free(stream);
    }
    else {
        // Write the buffer as-is
        memcpy(&m_Data.get()[offset],
               entry->data,
               entry->length);
        offset += entry->length;
    }
}
