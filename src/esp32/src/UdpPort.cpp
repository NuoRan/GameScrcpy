#include "gs/esp32/UdpPort.h"

#include <gs_esp32_protocol.h>

#include <gs/platform/UdpSocket.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <mutex>

namespace gs::esp32 {

namespace {

constexpr std::size_t kMostDatagram = 1500;

std::optional<platform::UdpEndpoint> resolve(const HostAndPort& target)
{
    if (std::optional<platform::UdpEndpoint> literal = platform::UdpEndpoint::parse(target.host, target.port)) {
        return literal;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(target.host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr) {
        return std::nullopt;
    }
    platform::UdpEndpoint endpoint;
    endpoint.port = target.port;
    const auto* address = reinterpret_cast<const sockaddr_in*>(found->ai_addr);
    std::memcpy(endpoint.address.data(), &address->sin_addr, 4);
    freeaddrinfo(found);
    return endpoint;
}

}

std::optional<HostAndPort> parseAddress(std::string_view address, std::uint16_t defaultPort)
{
    while (!address.empty() && address.front() == ' ') {
        address.remove_prefix(1);
    }
    while (!address.empty() && address.back() == ' ') {
        address.remove_suffix(1);
    }
    if (address.empty() || address.size() > 253) {
        return std::nullopt;
    }
    HostAndPort parsed{.host = std::string(address), .port = defaultPort};
    const std::size_t colon = address.rfind(':');
    if (colon != std::string_view::npos && address.find(':') == colon) {
        const std::string_view digits = address.substr(colon + 1);
        unsigned value = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size() || value == 0 ||
            value > 65535) {
            return std::nullopt;
        }
        parsed.host = std::string(address.substr(0, colon));
        parsed.port = static_cast<std::uint16_t>(value);
    }
    if (parsed.host.empty() || std::ranges::any_of(parsed.host, [](char c) {
            return c == ' ' || c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20;
        })) {
        return std::nullopt;
    }
    return parsed;
}

struct UdpPort::Impl {
    platform::UdpSocket socket;
    platform::UdpEndpoint peer;
    std::vector<BoardKey> keys;
    mutable std::mutex mutex;
    std::optional<std::size_t> matched;
};

UdpPort::UdpPort(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

UdpPort::~UdpPort() = default;

std::unique_ptr<UdpPort> UdpPort::open(const HostAndPort& target, std::vector<BoardKey> keys)
{
    if (keys.empty()) {
        return nullptr;
    }
    std::optional<platform::UdpSocket> socket = platform::UdpSocket::bind(platform::UdpEndpoint::any(false, 0));
    if (!socket) {
        return nullptr;
    }
    const std::optional<platform::UdpEndpoint> resolved = resolve(target);
    if (!resolved || resolved->v6) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->socket = std::move(*socket);
    impl->peer = *resolved;
    impl->keys = std::move(keys);
    return std::unique_ptr<UdpPort>(new UdpPort(std::move(impl)));
}

bool UdpPort::write(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() > static_cast<std::size_t>(GS_ESP32_MAX_FRAME)) {
        return false;
    }
    std::vector<const BoardKey*> keys;
    {
        const std::lock_guard lock(m_impl->mutex);
        if (m_impl->matched) {
            keys.push_back(&m_impl->keys[*m_impl->matched]);
        } else {
            for (const BoardKey& key : m_impl->keys) {
                keys.push_back(&key);
            }
        }
    }
    std::array<std::uint8_t, GS_ESP32_MAX_DATAGRAM> datagram{};
    std::copy(bytes.begin(), bytes.end(), datagram.begin());
    bool sent = false;
    for (const BoardKey* key : keys) {
        gs_esp32_tag(key->data(), datagram.data(), bytes.size(), datagram.data() + bytes.size());
        sent = m_impl->socket.sendTo(m_impl->peer, {datagram.data(), bytes.size() + GS_ESP32_TAG_SIZE}) || sent;
    }
    return sent;
}

std::size_t UdpPort::read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::array<std::uint8_t, kMostDatagram> datagram{};
    for (;;) {
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            return 0;
        }
        m_impl->socket.setReceiveTimeout(left);
        const std::optional<platform::UdpSocket::Received> received = m_impl->socket.receive(datagram);
        if (!received) {
            return 0;
        }
        if (!(received->from == m_impl->peer) || received->size < static_cast<std::size_t>(GS_ESP32_FRAME_OVERHEAD) +
                                                                      GS_ESP32_TAG_SIZE) {
            continue;
        }
        const std::size_t frameSize = received->size - GS_ESP32_TAG_SIZE;
        if (frameSize > buffer.size()) {
            continue;
        }
        std::optional<std::size_t> accepted;
        {
            const std::lock_guard lock(m_impl->mutex);
            if (m_impl->matched) {
                if (gs_esp32_tag_matches(m_impl->keys[*m_impl->matched].data(), datagram.data(), frameSize,
                                         datagram.data() + frameSize)) {
                    accepted = m_impl->matched;
                }
            } else {
                for (std::size_t index = 0; index < m_impl->keys.size(); ++index) {
                    if (gs_esp32_tag_matches(m_impl->keys[index].data(), datagram.data(), frameSize,
                                             datagram.data() + frameSize)) {
                        accepted = index;
                        m_impl->matched = index;
                        break;
                    }
                }
            }
        }
        if (!accepted) {
            continue;
        }
        std::copy_n(datagram.begin(), frameSize, buffer.begin());
        return frameSize;
    }
}

bool UdpPort::setBaud(std::uint32_t)
{
    return false;
}

bool UdpPort::setDtr(bool)
{
    return false;
}

bool UdpPort::setRts(bool)
{
    return false;
}

void UdpPort::discardInput() {}

void UdpPort::interrupt()
{
    m_impl->socket.interrupt();
}

bool UdpPort::datagrams() const
{
    return true;
}

bool UdpPort::gone() const
{
    return false;
}

std::optional<BoardKey> UdpPort::matchedKey() const
{
    const std::lock_guard lock(m_impl->mutex);
    if (!m_impl->matched) {
        return std::nullopt;
    }
    return m_impl->keys[*m_impl->matched];
}

std::string UdpPort::peer() const
{
    return m_impl->peer.toString();
}

}
