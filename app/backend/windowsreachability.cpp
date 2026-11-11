#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>

#include "windowsreachability.h"

#include <climits>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>

namespace {

WindowsReachability::Address readAddress(const SOCKET_ADDRESS& address)
{
    WindowsReachability::Address result;
    const auto* socket = address.lpSockaddr;
    if (!socket || address.iSockaddrLength < static_cast<int>(sizeof(sockaddr))) {
        return result;
    }
    if (socket->sa_family == AF_INET && address.iSockaddrLength >= static_cast<int>(sizeof(sockaddr_in))) {
        const auto& ip = reinterpret_cast<const sockaddr_in*>(socket)->sin_addr;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&ip);
        std::copy_n(bytes, 4, result.bytes.begin());
        result.size = 4;
    } else if (socket->sa_family == AF_INET6 && address.iSockaddrLength >= static_cast<int>(sizeof(sockaddr_in6))) {
        const auto& ip = reinterpret_cast<const sockaddr_in6*>(socket)->sin6_addr;
        std::copy_n(ip.s6_addr, 16, result.bytes.begin());
        result.size = 16;
    }
    return result;
}

std::string utf8(const wchar_t* text)
{
    if (!text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr)) {
        return {};
    }
    result.pop_back();
    return result;
}

} // namespace

ReachabilityPolicy::Route WindowsReachability::classify(const Address& local, const Address& peer)
{
    using ReachabilityPolicy::Route;
    if (local.size != 4 && local.size != 16) {
        return Route::Unknown;
    }

    // Keep the Windows adapter order used by the Qt path when addresses repeat.
    constexpr ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_DNS_SERVER |
                            GAA_FLAG_SKIP_MULTICAST;
    IP_ADAPTER_ADDRESSES initial[2]{};
    auto* adapters = initial;
    ULONG size = sizeof(initial);
    std::unique_ptr<void, decltype(&std::free)> buffer(nullptr, std::free);
    auto status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
    if (status == ERROR_BUFFER_OVERFLOW) {
        buffer.reset(std::malloc(size));
        if (!buffer) {
            return Route::Unknown;
        }
        adapters = static_cast<IP_ADAPTER_ADDRESSES*>(buffer.get());
        status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
    }
    if (status != ERROR_SUCCESS) {
        return Route::Unknown;
    }

    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        for (auto* entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
            if (entry->DadState == IpDadStateInvalid) {
                continue;
            }
            const auto address = readAddress(entry->Address);
            if (address.size != local.size || address.bytes != local.bytes) {
                continue;
            }

            wchar_t interfaceName[IF_MAX_STRING_SIZE + 1]{};
            auto name = ConvertInterfaceLuidToNameW(&adapter->Luid, interfaceName,
                                                   std::size(interfaceName)) == NO_ERROR
                ? utf8(interfaceName) : std::string{};
            if (name.empty() && adapter->AdapterName) {
                name = adapter->AdapterName;
            }
            const auto friendlyName = utf8(adapter->FriendlyName);
            const bool ppp = adapter->IfType == IF_TYPE_PPP;
            const bool tapPrefix = adapter->PhysicalAddressLength >= 2 &&
                                   adapter->PhysicalAddress[0] == 0 &&
                                   adapter->PhysicalAddress[1] == 0xff;
            const int prefix = entry->OnLinkPrefixLength;
            const bool onLink = peer.size == local.size && prefix <= local.size * 8 &&
                ReachabilityPolicy::sameSubnet(local.bytes.data(), peer.bytes.data(), local.size, prefix);
            return ReachabilityPolicy::classify({
                ppp, static_cast<int>(std::min<ULONG>(adapter->Mtu, INT_MAX)), ppp, true,
                name, tapPrefix ? "00:FF" : "", friendlyName.empty() ? name : friendlyName
            }, onLink);
        }
    }
    return Route::Unknown;
}
