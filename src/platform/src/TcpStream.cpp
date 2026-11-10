#include "gs/platform/TcpStream.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <algorithm>
#include <climits>
#include <utility>

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

void setTimeoutOption(std::uintptr_t socket, int option, std::chrono::milliseconds timeout) noexcept
{
    const auto milliseconds = static_cast<DWORD>(std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, MAXDWORD));
    setsockopt(native(socket), SOL_SOCKET, option, reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
}

}

TcpStream::TcpStream(TcpStream&& other) noexcept : m_socket(std::exchange(other.m_socket, kInvalid)) {}

TcpStream& TcpStream::operator=(TcpStream&& other) noexcept
{
    if (this != &other) {
        close();
        m_socket = std::exchange(other.m_socket, kInvalid);
    }
    return *this;
}

TcpStream::~TcpStream()
{
    close();
}

void TcpStream::close() noexcept
{
    if (m_socket != kInvalid) {
        closesocket(native(m_socket));
        m_socket = kInvalid;
    }
}

std::optional<TcpStream> TcpStream::connectLoopback(std::uint16_t port)
{
    if (!ensureWinsock()) {
        return std::nullopt;
    }
    const SOCKET handle =
        WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (handle == INVALID_SOCKET) {
        return std::nullopt;
    }
    TcpStream stream(static_cast<std::uintptr_t>(handle));

    TCP_INITIAL_RTO_PARAMETERS rto{TCP_INITIAL_RTO_UNSPECIFIED_RTT, TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS};
    DWORD returned = 0;
    WSAIoctl(handle, SIO_TCP_INITIAL_RTO, &rto, sizeof(rto), nullptr, 0, &returned, nullptr, nullptr);
    const BOOL noDelay = TRUE;
    setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return std::nullopt;
    }
    return stream;
}

void TcpStream::setReadTimeout(std::chrono::milliseconds timeout) noexcept
{
    setTimeoutOption(m_socket, SO_RCVTIMEO, timeout);
}

void TcpStream::setWriteTimeout(std::chrono::milliseconds timeout) noexcept
{
    setTimeoutOption(m_socket, SO_SNDTIMEO, timeout);
}

bool TcpStream::writeAll(std::span<const std::uint8_t> bytes) noexcept
{
    while (!bytes.empty()) {
        const int sent = send(native(m_socket), reinterpret_cast<const char*>(bytes.data()), clampedLength(bytes.size()), 0);
        if (sent <= 0) {
            return false;
        }
        bytes = bytes.subspan(static_cast<std::size_t>(sent));
    }
    return true;
}

bool TcpStream::writeAll(std::string_view text) noexcept
{
    return writeAll(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

bool TcpStream::readExact(std::span<std::uint8_t> bytes) noexcept
{
    while (!bytes.empty()) {
        const std::size_t received = readSome(bytes);
        if (received == 0) {
            return false;
        }
        bytes = bytes.subspan(received);
    }
    return true;
}

std::size_t TcpStream::readSome(std::span<std::uint8_t> bytes) noexcept
{
    const int received = recv(native(m_socket), reinterpret_cast<char*>(bytes.data()), clampedLength(bytes.size()), 0);
    m_readTimedOut = received == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT;
    return received > 0 ? static_cast<std::size_t>(received) : 0;
}

void TcpStream::interrupt() noexcept
{
    if (m_socket == kInvalid) {
        return;
    }
    shutdown(native(m_socket), SD_BOTH);
    CancelIoEx(reinterpret_cast<HANDLE>(m_socket), nullptr);
}

}
