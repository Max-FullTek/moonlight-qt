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
    return 0;
}
