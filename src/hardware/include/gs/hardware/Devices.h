#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::hardware {

enum class Kind : std::uint8_t { Serial, Hid, Ble };
inline constexpr std::array<std::string_view, 3> kKindNames{"serial", "hid", "ble"};

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::size_t kMostWriteBytes = 64 * 1024;
inline constexpr std::size_t kMostBleValueBytes = 512;
inline constexpr std::size_t kMostReportBytes = 4096;

struct DeviceInfo {
    std::string id;
    Kind kind = Kind::Serial;
    std::string name;
    std::string path;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::vector<std::string> services;

    friend bool operator==(const DeviceInfo&, const DeviceInfo&) = default;
};

struct Filter {
    std::optional<std::uint16_t> vendorId;
    std::optional<std::uint16_t> productId;
    std::optional<std::uint16_t> usagePage;
    std::optional<std::uint16_t> usage;
    std::vector<std::string> services;
    std::string namePrefix;

    friend bool operator==(const Filter&, const Filter&) = default;
};

enum class Parity : std::uint8_t { None, Even, Odd, Mark, Space };
inline constexpr std::array<std::string_view, 5> kParityNames{"none", "even", "odd", "mark", "space"};

enum class StopBits : std::uint8_t { One, OneAndHalf, Two };

enum class FlowControl : std::uint8_t { None, Hardware, Software };
inline constexpr std::array<std::string_view, 3> kFlowControlNames{"none", "hardware", "software"};

inline constexpr std::uint32_t kLeastBaudRate = 50;
inline constexpr std::uint32_t kMostBaudRate = 4'000'000;

struct SerialOptions {
    std::uint32_t baudRate = 115200;
    std::uint8_t dataBits = 8;
    Parity parity = Parity::None;
    StopBits stopBits = StopBits::One;
    FlowControl flowControl = FlowControl::None;
    bool dtr = true;
    bool rts = true;

    friend bool operator==(const SerialOptions&, const SerialOptions&) = default;
};

struct BleCharacteristic {
    std::string uuid;
    std::vector<std::string> properties;

    friend bool operator==(const BleCharacteristic&, const BleCharacteristic&) = default;
};

struct BleService {
    std::string uuid;
    std::vector<BleCharacteristic> characteristics;

    friend bool operator==(const BleService&, const BleService&) = default;
};

enum class Closed : std::uint8_t { Removed, Failed };

[[nodiscard]] std::string_view kindName(Kind kind) noexcept;
[[nodiscard]] std::optional<Kind> kindNamed(std::string_view name) noexcept;
[[nodiscard]] std::optional<std::string> canonicalUuid(std::string_view text);
[[nodiscard]] bool matches(const DeviceInfo& device, Kind kind, const std::vector<Filter>& filters);
[[nodiscard]] std::string hex4(std::uint16_t value);
[[nodiscard]] std::string addressText(std::uint64_t address);
[[nodiscard]] std::string bleId(std::uint64_t address);
[[nodiscard]] std::optional<std::uint64_t> bleAddressOf(std::string_view id);
[[nodiscard]] std::string detailOf(const DeviceInfo& device);
[[nodiscard]] bool validSerialOptions(const SerialOptions& options) noexcept;

}
