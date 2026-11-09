#pragma once

// Worker-side values. The Qt preferences retain their own Q_ENUM declarations
// with these explicit values so saved settings and QML names remain unchanged.
namespace VideoDecoderOptions {
enum Decoder {
    Auto = 0,
    Hardware = 1,
    Software = 2,
};

enum Renderer {
    ProbeOnly = -1,
    Automatic = 0,
    Vulkan = 1,
    Metal = 2,
    AvSampleBuffer = 3,
};
}

struct VideoResolution {
    int width;
    int height;
};
