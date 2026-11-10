#pragma once

#include "gs/adb/AdbClient.h"

#include <string>
#include <string_view>

namespace gs::adb {

[[nodiscard]] bool isGenericSerial(std::string_view serial) noexcept;

[[nodiscard]] std::string stableDeviceId(std::string_view serialNo, std::string_view bootSerialNo,
                                         std::string_view androidId);

struct DeviceInfo {
    std::string id;
    std::string androidRelease;
    std::string manufacturer;
    std::string model;
    std::string marketName;

    friend bool operator==(const DeviceInfo&, const DeviceInfo&) = default;
};

[[nodiscard]] AdbResult<DeviceInfo> readDeviceInfo(const AdbClient& client, std::string_view serial);

}
