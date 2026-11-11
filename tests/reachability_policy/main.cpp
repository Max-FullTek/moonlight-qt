#include "../../app/backend/reachabilitypolicy.h"

#include <iostream>

int main()
{
    using namespace ReachabilityPolicy;
    struct Case {
        const char* name;
        Interface nic;
        bool onLink;
        Route expected;
    };
    const Case cases[] = {
        {"LAN at MTU 1500", {false, 1500, false, true, "", "", ""}, true, Route::Lan},
        {"unknown MTU", {false, 0, false, true, "", "", ""}, true, Route::Lan},
        {"off-link Ethernet", {false, 1500, false, true, "", "", ""}, false, Route::Unknown},
        {"virtual or PPP", {true, 1500, false, true, "", "", ""}, true, Route::Vpn},
        {"small MTU", {false, 1499, false, true, "", "", ""}, true, Route::Vpn},
        {"point-to-point", {false, 1500, true, true, "", "", ""}, true, Route::Vpn},
        {"Windows virtual type", {false, 1500, false, true, "iftype53_1", "", ""}, true, Route::Vpn},
        {"Windows tunnel type", {false, 1500, false, true, "iftype131_1", "", ""}, false, Route::Vpn},
        {"Windows type on other OS", {false, 1500, false, false, "iftype53_1", "", ""}, true, Route::Lan},
        {"mixed case TAP MAC", {false, 1500, false, true, "", "00:fF:00:12:34:56", ""}, true, Route::Vpn},
        {"ZeroTier", {false, 1500, false, true, "", "", "ZeroTier One"}, true, Route::Vpn},
        {"VPN display name", {false, 1500, false, true, "", "", "Corp VPN 1"}, true, Route::Vpn},
        {"case-sensitive display name", {false, 1500, false, true, "", "", "vpn"}, true, Route::Lan},
    };
    for (const auto& test : cases) {
        if (classify(test.nic, test.onLink) != test.expected) {
            std::cerr << "Unexpected route: " << test.name << '\n';
            return 1;
        }
    }
    const auto local = ipv4Bytes(0xc0a80101);
    const auto peer = ipv4Bytes(0xc0a80181);
    auto ipv6Local = std::array<std::uint8_t, 16>{0x20, 0x01, 0x0d, 0xb8};
    auto ipv6Peer = ipv6Local;
    ipv6Peer[8] = 0x80;
    if (!sameSubnet(local.data(), peer.data(), 4, 0) ||
        !sameSubnet(local.data(), peer.data(), 4, 24) ||
        sameSubnet(local.data(), peer.data(), 4, 25) ||
        sameSubnet(local.data(), peer.data(), 4, -1) ||
        sameSubnet(local.data(), peer.data(), 4, 33) ||
        !sameSubnet(ipv6Local.data(), ipv6Peer.data(), 16, 64) ||
        sameSubnet(ipv6Local.data(), ipv6Peer.data(), 16, 65) ||
        !sameSubnet(ipv6Local.data(), ipv6Local.data(), 16, 129)) {
        std::cerr << "Unexpected subnet match\n";
        return 1;
    }
    return 0;
}
