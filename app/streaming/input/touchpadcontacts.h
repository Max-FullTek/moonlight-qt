#pragma once

#include <Limelight.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

namespace TouchpadContacts {
constexpr int MaxContacts = 5;
struct Contact {
    std::uint8_t eventType;
    std::uint32_t pointerId;
    float x;
    float y;
    float pressure;
};
using Active = std::unordered_map<std::uint32_t, Contact>;

struct Frame {
    Active active;
    std::array<Contact, MaxContacts> releases;
    std::array<Contact, MaxContacts> updates;
    int releaseCount = 0;
    int updateCount = 0;
};

// Inputs are the already-parsed Windows HID frame. Device ownership, button
// fallback and transport selection stay with the input handler.
inline Frame buildFrame(const Active& previous, const std::uint32_t* ids,
                         const float* x, const float* y, const float* pressure,
                         const std::uint8_t* touching, int count)
{
    Frame frame;
    for (int i = 0; i < count && i < MaxContacts; ++i) {
        if (!touching[i]) continue;
        const Contact contact{
            static_cast<std::uint8_t>(previous.find(ids[i]) != previous.end()
                    ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_DOWN),
            ids[i], std::clamp(x[i], 0.0f, 1.0f), std::clamp(y[i], 0.0f, 1.0f),
            std::clamp(pressure[i], 0.0f, 1.0f)};
        frame.active[contact.pointerId] = contact;
        frame.updates[frame.updateCount++] = contact;
    }
    for (const auto& entry : previous) {
        if (frame.active.find(entry.first) != frame.active.end()) continue;
        Contact contact = entry.second;
        contact.eventType = LI_TOUCH_EVENT_UP;
        frame.releases[frame.releaseCount++] = contact;
    }
    return frame;
}
} // namespace TouchpadContacts
