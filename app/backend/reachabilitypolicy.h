#pragma once

#include <string_view>

namespace ReachabilityPolicy {

enum class Route { Unknown, Lan, Vpn };

struct Interface {
    bool virtualOrPpp;
    int mtu;
    bool pointToPoint;
    bool windows;
    std::string_view systemName;
    std::string_view hardwareAddress;
    std::string_view displayName;
};

inline Route classify(const Interface& nic, bool onLink)
{
    const bool windowsVpnType = nic.windows &&
        (nic.systemName.substr(0, 9) == "iftype53_" ||
         nic.systemName.substr(0, 10) == "iftype131_");
    const auto macPrefix = nic.hardwareAddress.substr(0, 5);
    const bool tapAddress = macPrefix == "00:FF" || macPrefix == "00:Ff" ||
                            macPrefix == "00:fF" || macPrefix == "00:ff";
    if (nic.virtualOrPpp || (nic.mtu != 0 && nic.mtu < 1500) ||
        nic.pointToPoint || windowsVpnType || tapAddress ||
        nic.displayName.substr(0, 8) == "ZeroTier" ||
        nic.displayName.find("VPN") != std::string_view::npos) {
        return Route::Vpn;
    }
    return onLink ? Route::Lan : Route::Unknown;
}

} // namespace ReachabilityPolicy
