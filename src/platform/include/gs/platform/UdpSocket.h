#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gs::platform {

struct UdpEndpoint {
    std::array<std::uint8_t, 16> address{};
    std::uint16_t port = 0;
    bool v6 = false;

    [[nodiscard]] static std::optional<UdpEndpoint> parse(std::string_view host, std::uint16_t port);
    [[nodiscard]] static UdpEndpoint loopback(std::uint16_t port) noexcept;
    [[nodiscard]] static UdpEndpoint any(bool v6, std::uint16_t port) noexcept;
    [[nodiscard]] std::string toString() const;

    bool operator==(const UdpEndpoint&) const = default;
};

class UdpSocket
{
public:
    struct Received {
        std::size_t size = 0;
        UdpEndpoint from;
    };

    UdpSocket() noexcept = default;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    ~UdpSocket();

    [[nodiscard]] static std::optional<UdpSocket> bind(const UdpEndpoint& local);

    [[nodiscard]] std::uint16_t localPort() const noexcept;

    void setReceiveTimeout(std::chrono::milliseconds timeout) noexcept;
    bool setReceiveBufferSize(std::size_t bytes) noexcept;

    [[nodiscard]] bool sendTo(const UdpEndpoint& to, std::span<const std::uint8_t> datagram) noexcept;
    [[nodiscard]] std::optional<Received> receive(std::span<std::uint8_t> buffer) noexcept;
    [[nodiscard]] bool receiveTimedOut() const noexcept { return m_receiveTimedOut; }

    void interrupt() noexcept;

private:
    static constexpr std::uintptr_t kInvalid = ~std::uintptr_t{0};

    explicit UdpSocket(std::uintptr_t socket, bool v6) noexcept : m_socket(socket), m_v6(v6) {}
    void close() noexcept;

    std::uintptr_t m_socket = kInvalid;
    bool m_v6 = false;
    std::atomic<bool> m_interrupted{false};
    bool m_receiveTimedOut = false;
};

}
