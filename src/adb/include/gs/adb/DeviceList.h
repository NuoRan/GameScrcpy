#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs::adb {

enum class DeviceState : std::uint8_t {
    Device,
    Offline,
    Unauthorized,
    Authorizing,
    Connecting,
    Recovery,
    Sideload,
    Bootloader,
    NoPermissions,
    Host,
    Unknown,
};

struct DeviceEntry {
    std::string serial;
    DeviceState state = DeviceState::Unknown;
    std::string product;
    std::string model;
    std::string device;
    std::uint64_t transportId = 0;

    friend bool operator==(const DeviceEntry&, const DeviceEntry&) = default;
};

[[nodiscard]] std::vector<DeviceEntry> parseDeviceList(std::string_view text);

[[nodiscard]] DeviceState parseDeviceState(std::string_view text) noexcept;

}
