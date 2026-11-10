#include "gs/adb/DeviceList.h"

#include <charconv>

namespace gs::adb {

namespace {

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r';
}

std::vector<std::string_view> tokens(std::string_view line)
{
    std::vector<std::string_view> result;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && !isSpace(line[i])) {
            ++i;
        }
        if (i > start) {
            result.push_back(line.substr(start, i - start));
        }
    }
    return result;
}

}

DeviceState parseDeviceState(std::string_view text) noexcept
{
    struct Name {
        std::string_view text;
        DeviceState state;
    };
    static constexpr Name kNames[] = {
        {"device", DeviceState::Device},         {"offline", DeviceState::Offline},
        {"unauthorized", DeviceState::Unauthorized}, {"authorizing", DeviceState::Authorizing},
        {"connecting", DeviceState::Connecting}, {"recovery", DeviceState::Recovery},
        {"sideload", DeviceState::Sideload},     {"bootloader", DeviceState::Bootloader},
        {"no", DeviceState::NoPermissions},      {"host", DeviceState::Host},
    };
    for (const Name& name : kNames) {
        if (name.text == text) {
            return name.state;
        }
    }
    return DeviceState::Unknown;
}

std::vector<DeviceEntry> parseDeviceList(std::string_view text)
{
    std::vector<DeviceEntry> devices;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        const std::vector<std::string_view> parts = tokens(line);
        if (parts.size() < 2) {
            continue;
        }
        DeviceEntry entry;
        entry.serial = std::string(parts[0]);
        entry.state = parseDeviceState(parts[1]);
        for (std::size_t i = 2; i < parts.size(); ++i) {
            const std::size_t colon = parts[i].find(':');
            if (colon == std::string_view::npos) {
                continue;
            }
            const std::string_view key = parts[i].substr(0, colon);
            const std::string_view value = parts[i].substr(colon + 1);
            if (key == "product") {
                entry.product = std::string(value);
            } else if (key == "model") {
                entry.model = std::string(value);
            } else if (key == "device") {
                entry.device = std::string(value);
            } else if (key == "transport_id") {
                std::from_chars(value.data(), value.data() + value.size(), entry.transportId);
            }
        }
        devices.push_back(std::move(entry));
    }
    return devices;
}

}
