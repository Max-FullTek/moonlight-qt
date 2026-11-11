#pragma once

#include "reachabilitypolicy.h"

namespace WindowsReachability {

struct Address {
    std::array<std::uint8_t, 16> bytes{};
    int size = 0;
};

ReachabilityPolicy::Route classify(const Address& local, const Address& peer);

} // namespace WindowsReachability
