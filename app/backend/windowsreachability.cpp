#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include "windowsreachability.h"

#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto ProbeBudget = std::chrono::seconds(3);

struct Winsock {
    WSADATA data{};
    const bool ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    ~Winsock() { if (ready) WSACleanup(); }
};

struct ProbeSocket {
    SOCKET handle;
    ~ProbeSocket() { if (handle != INVALID_SOCKET) closesocket(handle); }
};

bool connectBefore(SOCKET socket, const ADDRINFOW& endpoint, Clock::time_point deadline)
{
    if (Clock::now() >= deadline) {
        return false;
    }
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) {
        return false;
    }
    if (connect(socket, endpoint.ai_addr, static_cast<int>(endpoint.ai_addrlen)) == 0) {
        return true;
    }
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
        return false;
    }

    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(socket, &writable);
    FD_SET(socket, &failed);
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now());
    if (remaining.count() <= 0) {
        return false;
    }
    const timeval timeout{static_cast<long>(remaining.count() / 1000000),
                          static_cast<long>(remaining.count() % 1000000)};
    return select(0, nullptr, &writable, &failed, &timeout) > 0 &&
        FD_ISSET(socket, &writable) && !FD_ISSET(socket, &failed);
}

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

std::optional<ReachabilityPolicy::Route> probeEndpoint(const ADDRINFOW& endpoint,
                                                      Clock::time_point deadline)
{
    const ProbeSocket socket{::socket(endpoint.ai_family, SOCK_STREAM, IPPROTO_TCP)};
    if (socket.handle == INVALID_SOCKET) {
        return std::nullopt;
    }
    if (endpoint.ai_family == AF_INET6) {
        const auto* address = reinterpret_cast<const sockaddr_in6*>(endpoint.ai_addr);
        if (IN6_IS_ADDR_V4MAPPED(&address->sin6_addr)) {
            const DWORD ipv6Only = 0;
            if (setsockopt(socket.handle, IPPROTO_IPV6, IPV6_V6ONLY,
                           reinterpret_cast<const char*>(&ipv6Only), sizeof(ipv6Only)) != 0) {
                return std::nullopt;
            }
        }
    }
    if (!connectBefore(socket.handle, endpoint, deadline)) {
        return std::nullopt;
    }
    sockaddr_storage local{};
    sockaddr_storage peer{};
    int localSize = sizeof(local);
    int peerSize = sizeof(peer);
    auto* localAddress = reinterpret_cast<sockaddr*>(&local);
    auto* peerAddress = reinterpret_cast<sockaddr*>(&peer);
    if (getsockname(socket.handle, localAddress, &localSize) != 0 ||
        getpeername(socket.handle, peerAddress, &peerSize) != 0) {
        return ReachabilityPolicy::Route::Unknown;
    }
    return WindowsReachability::classify(readAddress({localAddress, localSize}),
                                         readAddress({peerAddress, peerSize}));
}

} // namespace

std::uint32_t WindowsReachability::interfaceIndex(const wchar_t* name)
{
    NET_LUID luid{};
    NET_IFINDEX index = 0;
    if (name && *name && ConvertInterfaceNameToLuidW(name, &luid) == NO_ERROR &&
        ConvertInterfaceLuidToIndex(&luid, &index) == NO_ERROR) {
        return index;
    }
    return 0;
}

ReachabilityPolicy::Route WindowsReachability::probeAddress(
    const Address& address, std::uint16_t port, std::uint32_t scope)
{
    using ReachabilityPolicy::Route;
    const Winsock winsock;
    if (!winsock.ready) {
        return Route::Unknown;
    }

    ADDRINFOW endpoint{};
    sockaddr_in ipv4{};
    sockaddr_in6 ipv6{};
    if (address.size == 4) {
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(port);
        std::memcpy(&ipv4.sin_addr, address.bytes.data(), 4);
        endpoint.ai_family = AF_INET;
        endpoint.ai_addr = reinterpret_cast<sockaddr*>(&ipv4);
        endpoint.ai_addrlen = sizeof(ipv4);
    } else if (address.size == 16) {
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(port);
        ipv6.sin6_scope_id = scope;
        std::copy_n(address.bytes.begin(), 16, ipv6.sin6_addr.s6_addr);
        endpoint.ai_family = AF_INET6;
        endpoint.ai_addr = reinterpret_cast<sockaddr*>(&ipv6);
        endpoint.ai_addrlen = sizeof(ipv6);
    } else {
        return Route::Unknown;
    }
    return probeEndpoint(endpoint, Clock::now() + ProbeBudget).value_or(Route::Unknown);
}

ReachabilityPolicy::Route WindowsReachability::probeHostname(const wchar_t* host, std::uint16_t port)
{
    using ReachabilityPolicy::Route;
    const auto deadline = Clock::now() + ProbeBudget;
    const Winsock winsock;
    if (!winsock.ready || !host || !*host) {
        return Route::Unknown;
    }
    ADDRINFOW hints{};
    hints.ai_family = AF_UNSPEC;
    ADDRINFOW* list = nullptr;
    // Like the former synchronous Qt lookup, the system resolver can block.
    // All subsequent connections share the remaining time.
    if (GetAddrInfoW(host, nullptr, &hints, &list) != 0 || !list) {
        return Route::Unknown;
    }
    const std::unique_ptr<ADDRINFOW, decltype(&FreeAddrInfoW)> addresses(list, FreeAddrInfoW);
    struct Candidate {
        ADDRINFOW* endpoint;
        Address address;
    };
    std::vector<Candidate> candidates;
    for (auto* node = list; node; node = node->ai_next) {
        const auto address = readAddress({node->ai_addr, static_cast<int>(node->ai_addrlen)});
        if (address.size == 0 || std::any_of(candidates.begin(), candidates.end(), [&](const Candidate& prior) {
                return prior.address.size == address.size && prior.address.bytes == address.bytes;
            })) {
            continue;
        }
        if (address.size == 4) {
            reinterpret_cast<sockaddr_in*>(node->ai_addr)->sin_port = htons(port);
        } else {
            reinterpret_cast<sockaddr_in6*>(node->ai_addr)->sin6_port = htons(port);
        }
        candidates.push_back({node, address});
    }
    // Preserve the resolver's order and the old socket's second pass.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& candidate : candidates) {
            if (Clock::now() >= deadline) {
                return Route::Unknown;
            }
            if (const auto route = probeEndpoint(*candidate.endpoint, deadline)) {
                return *route;
            }
        }
    }
    return Route::Unknown;
}

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
