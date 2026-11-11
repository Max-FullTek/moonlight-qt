#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include "windowsreachability.h"

#include <climits>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>

namespace {

struct Winsock {
    WSADATA data{};
    const bool ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    ~Winsock() { if (ready) WSACleanup(); }
};

struct ProbeSocket {
    SOCKET handle;
    ~ProbeSocket() { if (handle != INVALID_SOCKET) closesocket(handle); }
};

bool connectWithinThreeSeconds(SOCKET socket, const ADDRINFOW& endpoint)
{
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
    const timeval timeout{3, 0};
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

} // namespace

std::optional<ReachabilityPolicy::Route> WindowsReachability::probeNumeric(
    const wchar_t* host, std::uint16_t port)
{
    using ReachabilityPolicy::Route;
    const Winsock winsock;
    if (!winsock.ready) {
        return Route::Unknown;
    }
    ADDRINFOW hints{};
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const auto service = std::to_wstring(port);
    ADDRINFOW* endpoint = nullptr;
    if (GetAddrInfoW(host, service.c_str(), &hints, &endpoint) != 0 || !endpoint) {
        return std::nullopt;
    }
    const std::unique_ptr<ADDRINFOW, decltype(&FreeAddrInfoW)> address(endpoint, FreeAddrInfoW);
    const ProbeSocket socket{::socket(address->ai_family, address->ai_socktype, address->ai_protocol)};
    if (socket.handle == INVALID_SOCKET || !connectWithinThreeSeconds(socket.handle, *address)) {
        return Route::Unknown;
    }
    sockaddr_storage local{};
    sockaddr_storage peer{};
    int localSize = sizeof(local);
    int peerSize = sizeof(peer);
    auto* localAddress = reinterpret_cast<sockaddr*>(&local);
    auto* peerAddress = reinterpret_cast<sockaddr*>(&peer);
    if (getsockname(socket.handle, localAddress, &localSize) != 0 ||
        getpeername(socket.handle, peerAddress, &peerSize) != 0) {
        return Route::Unknown;
    }
    return classify(readAddress({localAddress, localSize}), readAddress({peerAddress, peerSize}));
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
