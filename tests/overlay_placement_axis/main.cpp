#include "streaming/video/overlayplacementaxis.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main()
{
    using namespace OverlayPlacementAxis;
    const auto landscape = bounds(100, 1000, 36, 10);
    require(landscape.start == 110 && landscape.end == 1054, "viewport margins");
    require(resolve(1.0, landscape) == 1054, "default right edge");
    require(resolve(normalize(777, landscape), landscape) == 777, "pixel round trip");
    const auto portrait = bounds(-300, 600, 36, 10);
    require(resolve(normalize(777, landscape), portrait) == 94, "resized negative-origin viewport");
    require(clamp(-999, portrait) == -290 && clamp(999, portrait) == 254, "clamp both edges");
    const auto narrow = bounds(20, 24, 36, 10);
    require(narrow.start == 20 && narrow.end == 20 && normalize(999, narrow) == 0.5,
            "undersized viewport collapses safely");
    const auto negativeMargin = bounds(0, 100, 36, -10);
    require(negativeMargin.start == 0 && negativeMargin.end == 64, "negative margin");
    const auto hugeMargin = bounds(0, 101, 36, 999);
    require(hugeMargin.start == 32 && hugeMargin.end == 33, "margin cannot invert bounds");
    require(resolve(-2.0, landscape) == 110 && resolve(4.0, landscape) == 1054,
            "clamp normalized positions");
    require(resolve(std::numeric_limits<double>::infinity(), landscape) == 582 &&
            resolve(std::numeric_limits<double>::quiet_NaN(), landscape) == 582,
            "non-finite positions use midpoint");
    require(resolve(0.5, {-10, -9}) == -10 && resolve(0.5, {9, 10}) == 10,
            "half pixels round away from zero");
    for (int origin : {-1920, -300, 0, 100, 3840}) {
        for (int length : {0, 24, 36, 600, 1080, 3840}) {
            const auto range = bounds(origin, length, 36, 10);
            for (int pixel = range.start; pixel <= range.end; ++pixel) {
                require(resolve(normalize(pixel, range), range) == pixel,
                        "all supported pixels round trip exactly");
            }
        }
    }
    std::puts("overlay_placement_axis=passed");
}
