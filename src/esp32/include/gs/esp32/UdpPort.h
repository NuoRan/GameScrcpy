#pragma once

#include "gs/esp32/Boards.h"
#include "gs/esp32/Port.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::esp32 {

struct HostAndPort {
    std::string host;
    std::uint16_t port = 0;

    friend bool operator==(const HostAndPort&, const HostAndPort&) = default;
};

[[nodiscard]] std::optional<HostAndPort> parseAddress(std::string_view address, std::uint16_t defaultPort);

class UdpPort final : public Port
{
public:
    ~UdpPort() override;
    UdpPort(const UdpPort&) = delete;
    UdpPort& operator=(const UdpPort&) = delete;

    [[nodiscard]] static std::unique_ptr<UdpPort> open(const HostAndPort& target, std::vector<BoardKey> keys);

    [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) override;
    [[nodiscard]] std::size_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override;
    [[nodiscard]] bool setBaud(std::uint32_t baud) override;
    [[nodiscard]] bool setDtr(bool on) override;
    [[nodiscard]] bool setRts(bool on) override;
    void discardInput() override;
    void interrupt() override;
    [[nodiscard]] bool datagrams() const override;
    [[nodiscard]] bool gone() const override;

    [[nodiscard]] std::optional<BoardKey> matchedKey() const;
    [[nodiscard]] std::string peer() const;

private:
    struct Impl;
    explicit UdpPort(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

}
