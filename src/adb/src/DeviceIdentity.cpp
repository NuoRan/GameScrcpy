#include "gs/adb/DeviceIdentity.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace gs::adb {

namespace {

bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}

bool isGenericSerial(std::string_view serial) noexcept
{
    if (serial.empty()) {
        return true;
    }
    if (std::all_of(serial.begin(), serial.end(), [&](char c) { return c == serial.front(); })) {
        return true;
    }
    constexpr std::string_view kEmulator = "emulator";
    if (serial.size() >= kEmulator.size() && equalsIgnoringCase(serial.substr(0, kEmulator.size()), kEmulator)) {
        return true;
    }
    static constexpr std::array<std::string_view, 5> kShared{
        "unknown", "0123456789abcdef", "1234567890abcdef", "123456789abcdef", "0123456789",
    };
    return std::any_of(kShared.begin(), kShared.end(), [&](std::string_view shared) {
        return equalsIgnoringCase(serial, shared);
    });
}

std::string stableDeviceId(std::string_view serialNo, std::string_view bootSerialNo, std::string_view androidId)
{
    if (!isGenericSerial(serialNo)) {
        return std::string(serialNo);
    }
    if (!isGenericSerial(bootSerialNo)) {
        return std::string(bootSerialNo);
    }
    if (!androidId.empty()) {
        return "aid:" + std::string(androidId);
    }
    return std::string(serialNo);
}

AdbResult<DeviceInfo> readDeviceInfo(const AdbClient& client, std::string_view serial)
{
    const auto result = client.shell(
        serial, "echo \"serialno=$(getprop ro.serialno)\"; echo \"bootserialno=$(getprop ro.boot.serialno)\"; "
                "echo \"androidid=$(settings get secure android_id 2>/dev/null)\"; "
                "echo \"release=$(getprop ro.build.version.release)\"; "
                "echo \"manufacturer=$(getprop ro.product.manufacturer)\"; "
                "echo \"model=$(getprop ro.product.model)\"; "
                "echo \"marketname=$(getprop ro.product.marketname)\"; "
                "echo \"booted=$(getprop sys.boot_completed)\"");
    if (!result) {
        return makeUnexpected(result.error());
    }
    static constexpr std::array<std::string_view, 8> kKeys{"serialno",     "bootserialno", "androidid", "release",
                                                           "manufacturer", "model",        "marketname", "booted"};
    std::array<std::string_view, kKeys.size()> values{};
    std::string_view rest = result->out;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
            line.remove_suffix(1);
        }
        const std::size_t equals = line.find('=');
        const auto key = std::find(kKeys.begin(), kKeys.end(), line.substr(0, equals));
        if (equals != std::string_view::npos && key != kKeys.end()) {
            values[static_cast<std::size_t>(key - kKeys.begin())] = line.substr(equals + 1);
        }
    }
    const std::string_view serialNo = values[0];
    const std::string_view bootSerialNo = values[1];
    const std::string_view androidId = values[2] == "null" ? std::string_view() : values[2];
    const bool tellable = values[7] == "1" || !isGenericSerial(serialNo) || !isGenericSerial(bootSerialNo) ||
                          !androidId.empty();
    return DeviceInfo{tellable ? stableDeviceId(serialNo, bootSerialNo, androidId) : std::string(),
                      std::string(values[3]), std::string(values[4]), std::string(values[5]), std::string(values[6])};
}

}
