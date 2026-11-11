#pragma once

#include "reachabilitypolicy.h"
#include <optional>

namespace WindowsReachability {

struct Address {
    std::array<std::uint8_t, 16> bytes{};
    int size = 0;
};

ReachabilityPolicy::Route classify(const Address& local, const Address& peer);

// Empty means Winsock could not interpret the address as a numeric endpoint.
// The caller can then use its existing hostname resolver.
std::optional<ReachabilityPolicy::Route> probeNumeric(const wchar_t* host, std::uint16_t port);

// host is the ASCII-compatible spelling used for DNS, widened to UTF-16.
ReachabilityPolicy::Route probeHostname(const wchar_t* host, std::uint16_t port);

} // namespace WindowsReachability
