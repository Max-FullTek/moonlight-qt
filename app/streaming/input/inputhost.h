#pragma once

#include <cstdint>
#include <optional>

// Services supplied by the stream owner. The SDL input handler owns input
// state and forwarding; the owner handles window actions and local controls.
class IInputHost
{
public:
    virtual ~IInputHost() = default;
    virtual bool isStreamingWindowFullscreen() const = 0;
    virtual bool usesCompositorWindow() const = 0;
    virtual bool shouldConfinePointerForCompositor() const = 0;
    virtual void toggleFullscreen() = 0;
    virtual void minimizeStreamingWindow() = 0;
    virtual void toggleStatisticsOverlay() = 0;
    virtual void quitHostAndExit() = 0;
    virtual void showInputControls() = 0;
    virtual void notifyMouseEmulationMode(bool enabled) = 0;
    virtual bool queueTouchpadFlush() = 0;
    virtual void applyControllerMappings() = 0;
    virtual std::optional<uint16_t> sensorReportRateLimit() const = 0;
    virtual bool queueCursorFlush() = 0;
};
