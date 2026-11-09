#pragma once

#include <Limelight.h>
#include <cstddef>
#include <cstdint>
#include <memory>

// Reusable compressed-packet storage. Valid until the next assemble() call;
// the caller must finish avcodec_send_packet() before reusing it.
class VideoDecodeBuffer {
public:
    VideoDecodeBuffer();
    int assemble(PDECODE_UNIT unit, bool fixSps, bool repackAv1);
    uint8_t* data() { return m_Data.get(); }

private:
    void writeEntry(PLENTRY entry, int& offset, bool fixSps);
    std::unique_ptr<uint8_t[]> m_Data;
    size_t m_Capacity;
};
