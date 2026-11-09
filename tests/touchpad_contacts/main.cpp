#include "streaming/input/touchpadcontacts.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {
void require(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main()
{
    using namespace TouchpadContacts;
    const std::uint32_t ids[]{0, 1, 0x80000000u, 0xffffffffu, 99, 100};
    const float x[]{-1, 0, 0.5f, 1, 2, 0.5f};
    const float y[]{2, 1, 0.5f, 0, -1, 0.5f};
    const float pressure[]{-1, 0, 0.25f, 1, 2, 0.5f};
    const std::uint8_t all[]{1, 1, 1, 1, 1, 1};
    const auto first = buildFrame({}, ids, x, y, pressure, all, 6);
    require(first.active.size() == 5 && first.updateCount == 5 && first.releaseCount == 0 &&
            first.active.find(100) == first.active.end(), "five-contact cap");
    require(first.updates[0].x == 0 && first.updates[0].y == 1 && first.updates[0].pressure == 0 &&
            first.updates[4].x == 1 && first.updates[4].y == 0 && first.updates[4].pressure == 1,
            "clamp coordinates and pressure");
    for (int oldMask = 0; oldMask < 32; ++oldMask) {
        std::uint8_t oldTouching[5];
        for (int i = 0; i < 5; ++i) oldTouching[i] = (oldMask >> i) & 1;
        const auto previous = buildFrame({}, ids, x, y, pressure, oldTouching, 5);
        for (int newMask = 0; newMask < 32; ++newMask) {
            std::uint8_t touching[5];
            for (int i = 0; i < 5; ++i) touching[i] = (newMask >> i) & 1;
            const auto next = buildFrame(previous.active, ids, x, y, pressure, touching, 5);
            int updateIndex = 0;
            int expectedReleases = 0;
            for (int i = 0; i < 5; ++i) {
                const bool wasTouching = oldTouching[i] != 0;
                const bool isTouching = touching[i] != 0;
                require((next.active.find(ids[i]) != next.active.end()) == isTouching,
                        "next active IDs reflect frame");
                require((previous.active.find(ids[i]) != previous.active.end()) == wasTouching,
                        "frame calculation does not mutate previous state");
                if (isTouching) {
                    const auto& update = next.updates[updateIndex++];
                    require(update.pointerId == ids[i] && update.eventType ==
                            (wasTouching ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_DOWN),
                            "retain HID order and distinguish Down/Move by full ID");
                }
                if (wasTouching && !isTouching) {
                    ++expectedReleases;
                    int matches = 0;
                    for (int release = 0; release < next.releaseCount; ++release) {
                        const auto& contact = next.releases[release];
                        if (contact.pointerId == ids[i]) {
                            ++matches;
                            require(contact.eventType == LI_TOUCH_EVENT_UP &&
                                    contact.x == previous.active.at(ids[i]).x &&
                                    contact.y == previous.active.at(ids[i]).y &&
                                    contact.pressure == previous.active.at(ids[i]).pressure,
                                    "release uses last known contact values");
                        }
                    }
                    require(matches == 1, "release each removed ID exactly once");
                }
            }
            require(next.updateCount == updateIndex && next.releaseCount == expectedReleases,
                    "no extra update or release events");
        }
    }
    const auto cleared = buildFrame(first.active, nullptr, nullptr, nullptr, nullptr, nullptr, 0);
    require(cleared.active.empty() && cleared.updateCount == 0 && cleared.releaseCount == 5,
            "empty frame releases all old contacts");
    const std::uint32_t replacementIds[]{10, 11, 12, 13, 14};
    const auto replacement = buildFrame(first.active, replacementIds, x, y, pressure, all, 5);
    require(replacement.releaseCount == 5 && replacement.updateCount == 5,
            "full replacement exposes releases separately before new Down batch");
    const std::uint32_t duplicateIds[]{10, 10};
    const auto duplicate = buildFrame({}, duplicateIds, x, y, pressure, all, 2);
    require(duplicate.active.size() == 1 && duplicate.updateCount == 2 &&
            duplicate.active.at(10).y == y[1],
            "retain duplicate-report event count and last-value storage");
    std::puts("touchpad_contacts=passed (1024 frame transitions, no input devices)");
}
