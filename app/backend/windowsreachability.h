#pragma once

#include "reachabilitypolicy.h"

namespace WindowsReachability {

struct Address {
    std::array<std::uint8_t, 16> bytes{};
    int size = 0;
};

ReachabilityPolicy::Route classify(const Address& local, const Address& peer);

std::uint32_t interfaceIndex(const wchar_t* name);
ReachabilityPolicy::Route probeAddress(const Address& address, std::uint16_t port,
                                      std::uint32_t scope = 0);

// host is the ASCII-compatible spelling used for DNS, widened to UTF-16.
ReachabilityPolicy::Route probeHostname(const wchar_t* host, std::uint16_t port);

} // namespace WindowsReachability
