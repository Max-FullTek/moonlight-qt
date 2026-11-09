#pragma once

#include <algorithm>
#include <cmath>

// Shared placement arithmetic. Callers retain responsibility for validating
// viewport/button sizes and persisting positions in their own UI framework.
namespace OverlayPlacementAxis {
struct Bounds
{
    int start;
    int end;
};

inline Bounds bounds(int origin, int parentLength, int buttonLength, int margin)
{
    const int movableLength = std::max(0, parentLength - buttonLength);
    const int safeMargin = std::min(std::max(0, margin), movableLength / 2);
    return {origin + safeMargin, origin + movableLength - safeMargin};
}

inline int clamp(int position, Bounds range)
{
    return std::clamp(position, range.start, range.end);
}

inline int resolve(double normalized, Bounds range)
{
    const double safe = std::isfinite(normalized)
            ? std::clamp(normalized, 0.0, 1.0) : 0.5;
    // Preserve qRound's half-away-from-zero pixel rounding.
    return static_cast<int>(std::round(range.start + (range.end - range.start) * safe));
}

inline double normalize(int position, Bounds range)
{
    const int length = range.end - range.start;
    if (length <= 0) {
        return 0.5;
    }
    return std::clamp(static_cast<double>(clamp(position, range) - range.start) / length,
                      0.0, 1.0);
}
} // namespace OverlayPlacementAxis
