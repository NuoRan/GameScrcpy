#include "gs/platform/UdpSocket.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <format>
#include <utility>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace gs::platform {

namespace {

bool ensureWinsock() noexcept
{
    static const bool ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}

SOCKET native(std::uintptr_t socket) noexcept
{
    return static_cast<SOCKET>(socket);
}

int clampedLength(std::size_t size) noexcept
{
    return static_cast<int>(std::min<std::size_t>(size, INT_MAX));
}

int toSockaddr(const UdpEndpoint& endpoint, sockaddr_storage& storage) noexcept
{
    std::memset(&storage, 0, sizeof(storage));
    if (endpoint.v6) {
        auto& address = reinterpret_cast<sockaddr_in6&>(storage);
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(endpoint.port);
        std::memcpy(&address.sin6_addr, endpoint.address.data(), 16);
        return sizeof(sockaddr_in6);
    }
    auto& address = reinterpret_cast<sockaddr_in&>(storage);
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    std::memcpy(&address.sin_addr, endpoint.address.data(), 4);
    return sizeof(sockaddr_in);
}

UdpEndpoint fromSockaddr(const sockaddr_storage& storage) noexcept
{
    UdpEndpoint endpoint;
    if (storage.ss_family == AF_INET6) {
        const auto& address = reinterpret_cast<const sockaddr_in6&>(storage);
        endpoint.v6 = true;
        endpoint.port = ntohs(address.sin6_port);
        std::memcpy(endpoint.address.data(), &address.sin6_addr, 16);
    } else {
        const auto& address = reinterpret_cast<const sockaddr_in&>(storage);
        endpoint.port = ntohs(address.sin_port);
        std::memcpy(endpoint.address.data(), &address.sin_addr, 4);
    }
    return endpoint;
}

}

std::optional<UdpEndpoint> UdpEndpoint::parse(std::string_view host, std::uint16_t port)
{
    if (host.empty() || host.size() >= INET6_ADDRSTRLEN) {
        return std::nullopt;
    }
    char text[INET6_ADDRSTRLEN]{};
    std::memcpy(text, host.data(), host.size());
    UdpEndpoint endpoint;
    endpoint.port = port;
    in_addr v4{};
    if (inet_pton(AF_INET, text, &v4) == 1) {
        std::memcpy(endpoint.address.data(), &v4, 4);
        return endpoint;
    }
    in6_addr v6{};
    if (inet_pton(AF_INET6, text, &v6) == 1) {
        endpoint.v6 = true;
        std::memcpy(endpoint.address.data(), &v6, 16);
        return endpoint;
    }
    return std::nullopt;
}

UdpEndpoint UdpEndpoint::loopback(std::uint16_t port) noexcept
{
    UdpEndpoint endpoint;
    endpoint.address = {127, 0, 0, 1};
    endpoint.port = port;
    return endpoint;
}

UdpEndpoint UdpEndpoint::any(bool v6, std::uint16_t port) noexcept
{
    UdpEndpoint endpoint;
    endpoint.v6 = v6;
    endpoint.port = port;
    return endpoint;
}

std::string UdpEndpoint::toString() const
{
    char text[INET6_ADDRSTRLEN]{};
    if (v6) {
        in6_addr raw{};
        std::memcpy(&raw, address.data(), 16);
        inet_ntop(AF_INET6, &raw, text, sizeof(text));
        return std::format("[{}]:{}", text, port);
    }
    in_addr raw{};
    std::memcpy(&raw, address.data(), 4);
    inet_ntop(AF_INET, &raw, text, sizeof(text));
    return std::format("{}:{}", text, port);
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : m_socket(std::exchange(other.m_socket, kInvalid)), m_v6(other.m_v6),
      m_interrupted(other.m_interrupted.load(std::memory_order_relaxed))
{
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other) {
        close();
        m_socket = std::exchange(other.m_socket, kInvalid);
        m_v6 = other.m_v6;
        m_interrupted.store(other.m_interrupted.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

UdpSocket::~UdpSocket()
{
    close();
}

void UdpSocket::close() noexcept
{
    if (m_socket != kInvalid) {
        closesocket(native(m_socket));
        m_socket = kInvalid;
    }
}

std::optional<UdpSocket> UdpSocket::bind(const UdpEndpoint& local)
{
    if (!ensureWinsock()) {
        return std::nullopt;
    }
    const SOCKET handle = WSASocketW(local.v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0,
                                     WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (handle == INVALID_SOCKET) {
        return std::nullopt;
    }
    UdpSocket socket(static_cast<std::uintptr_t>(handle), local.v6);
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(handle, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);

    sockaddr_storage storage{};
    const int length = toSockaddr(local, storage);
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&storage), length) != 0) {
        return std::nullopt;
    }
    return socket;
}

std::uint16_t UdpSocket::localPort() const noexcept
{
    sockaddr_storage storage{};
    int length = sizeof(storage);
    if (m_socket == kInvalid || getsockname(native(m_socket), reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
        return 0;
    }
    return fromSockaddr(storage).port;
}

void UdpSocket::setReceiveTimeout(std::chrono::milliseconds timeout) noexcept
{
    const auto milliseconds = static_cast<DWORD>(std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, MAXDWORD));
    setsockopt(native(m_socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&milliseconds),
               sizeof(milliseconds));
}

bool UdpSocket::setReceiveBufferSize(std::size_t bytes) noexcept
{
    const int size = clampedLength(bytes);
    return setsockopt(native(m_socket), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), sizeof(size)) == 0;
}

bool UdpSocket::sendTo(const UdpEndpoint& to, std::span<const std::uint8_t> datagram) noexcept
{
    if (m_interrupted.load(std::memory_order_acquire) || to.v6 != m_v6) {
        return false;
    }
    sockaddr_storage storage{};
    const int length = toSockaddr(to, storage);
    const int sent = sendto(native(m_socket), reinterpret_cast<const char*>(datagram.data()),
                            clampedLength(datagram.size()), 0, reinterpret_cast<const sockaddr*>(&storage), length);
    return sent == static_cast<int>(datagram.size());
}

std::optional<UdpSocket::Received> UdpSocket::receive(std::span<std::uint8_t> buffer) noexcept
{
    m_receiveTimedOut = false;
    while (!m_interrupted.load(std::memory_order_acquire)) {
        sockaddr_storage storage{};
        int length = sizeof(storage);
        const int received = recvfrom(native(m_socket), reinterpret_cast<char*>(buffer.data()),
                                      clampedLength(buffer.size()), 0, reinterpret_cast<sockaddr*>(&storage), &length);
        if (received >= 0) {
            return Received{static_cast<std::size_t>(received), fromSockaddr(storage)};
        }
        const int error = WSAGetLastError();
        if (error == WSAEMSGSIZE) {
            continue;
        }
        m_receiveTimedOut = error == WSAETIMEDOUT;
        break;
    }
    return std::nullopt;
}

void UdpSocket::interrupt() noexcept
{
    m_interrupted.store(true, std::memory_order_release);
    if (m_socket == kInvalid) {
        return;
    }
    shutdown(native(m_socket), SD_BOTH);
    CancelIoEx(reinterpret_cast<HANDLE>(m_socket), nullptr);
}

}
