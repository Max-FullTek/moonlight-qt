// Exercise the real static libraries without Qt, a host, or user data.
#include <Limelight.h>
#include <h264_stream.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main(void)
{
    STREAM_CONFIGURATION config;
    memset(&config, 0xA5, sizeof(config));
    LiInitializeStreamConfiguration(&config);
    CHECK(config.width == 0 && config.height == 0);
    CHECK(config.videoFecPercentage == -2);

    // This references Connection.c and forces the linker to resolve the actual
    // transport/crypto implementation, not just the standalone default helpers.
    CHECK(strcmp(LiGetStageName(STAGE_RTSP_HANDSHAKE), "RTSP handshake") == 0);

    uint8_t annex_b[] = {0, 0, 0, 1, 0x67, 0x42, 0, 0x1F, 0, 0, 1, 0x68};
    int start, end;
    CHECK(find_nal_unit(annex_b, sizeof(annex_b), &start, &end) == 4);
    CHECK(start == 4 && end == 8);
    CHECK((annex_b[start] & 0x1F) == 7);
    h264_stream_t* stream = h264_new();
    CHECK(stream != NULL && stream->nal != NULL && stream->sps != NULL);
    h264_free(stream);

    puts("Native core linked and executed without Qt; no streaming host used.");
    return 0;
}
