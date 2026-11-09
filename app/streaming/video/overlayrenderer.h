#pragma once

namespace Overlay {
enum OverlayType {
    OverlayDebug,
    OverlayStatusUpdate,
    OverlayMax
};

class IOverlayRenderer
{
public:
    virtual ~IOverlayRenderer() = default;
    virtual void notifyOverlayUpdated(OverlayType type) = 0;
};
}
