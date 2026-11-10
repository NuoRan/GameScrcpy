#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gs::platform {

class TcpStream
{
public:
    TcpStream() noexcept = default;
    TcpStream(TcpStream&& other) noexcept;
    TcpStream& operator=(TcpStream&& other) noexcept;
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    ~TcpStream();

    [[nodiscard]] static std::optional<TcpStream> connectLoopback(std::uint16_t port);

    void setReadTimeout(std::chrono::milliseconds timeout) noexcept;
    void setWriteTimeout(std::chrono::milliseconds timeout) noexcept;

    [[nodiscard]] bool writeAll(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool writeAll(std::string_view text) noexcept;
    [[nodiscard]] bool readExact(std::span<std::uint8_t> bytes) noexcept;
    [[nodiscard]] std::size_t readSome(std::span<std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool readTimedOut() const noexcept { return m_readTimedOut; }

    void interrupt() noexcept;

private:
    static constexpr std::uintptr_t kInvalid = ~std::uintptr_t{0};

    explicit TcpStream(std::uintptr_t socket) noexcept : m_socket(socket) {}
    void close() noexcept;

    std::uintptr_t m_socket = kInvalid;
    bool m_readTimedOut = false;
};

}
