#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::esp32 {

class Port
{
public:
    virtual ~Port() = default;

    [[nodiscard]] virtual bool write(std::span<const std::uint8_t> bytes) = 0;
    [[nodiscard]] virtual std::size_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) = 0;
    [[nodiscard]] virtual bool setBaud(std::uint32_t baud) = 0;
    [[nodiscard]] virtual bool setDtr(bool on) = 0;
    [[nodiscard]] virtual bool setRts(bool on) = 0;
    virtual void discardInput() = 0;
    virtual void interrupt() = 0;
    [[nodiscard]] virtual bool datagrams() const = 0;
    [[nodiscard]] virtual bool gone() const = 0;
};

enum class PortKind : std::uint8_t { EspressifUsb, Ch34x, Cp210x, Other };

inline constexpr std::uint16_t kEspressifVendor = 0x303A;
inline constexpr std::uint16_t kUsbSerialJtagProduct = 0x1001;
inline constexpr std::uint16_t kWchVendor = 0x1A86;
inline constexpr std::uint16_t kSiliconLabsVendor = 0x10C4;

struct SerialPortInfo {
    std::string name;
    std::string description;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    PortKind kind = PortKind::Other;

    friend bool operator==(const SerialPortInfo&, const SerialPortInfo&) = default;
};

enum class SerialFailure : std::uint8_t { None, NotFound, Busy, Refused };

[[nodiscard]] PortKind classifyPort(std::uint16_t vendorId, std::uint16_t productId) noexcept;
[[nodiscard]] bool usbSerialJtag(std::uint16_t vendorId, std::uint16_t productId) noexcept;
[[nodiscard]] bool probedAutomatically(PortKind kind) noexcept;
[[nodiscard]] bool bridge(PortKind kind) noexcept;
[[nodiscard]] std::optional<std::pair<std::uint16_t, std::uint16_t>> usbIdsOf(std::string_view instanceId) noexcept;
[[nodiscard]] bool validPortName(std::string_view name) noexcept;
[[nodiscard]] int portNumber(std::string_view name) noexcept;
[[nodiscard]] std::vector<SerialPortInfo> listSerialPorts();
[[nodiscard]] std::unique_ptr<Port> openSerialPort(std::string_view name, std::uint32_t baud, SerialFailure& failure);

}
