#include "gs/adb/WirelessServices.h"

#include <array>
#include <optional>

namespace gs::adb {

namespace {

std::optional<WirelessService::Kind> kindOf(std::string_view type) noexcept
{
    if (type.ends_with('.')) {
        type.remove_suffix(1);
    }
    if (type == "_adb-tls-pairing._tcp") {
        return WirelessService::Kind::Pairing;
    }
    if (type == "_adb-tls-connect._tcp") {
        return WirelessService::Kind::Connect;
    }
    return std::nullopt;
}

}

std::vector<WirelessService> parseWirelessServices(std::string_view text)
{
    std::vector<WirelessService> services;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }

        std::array<std::string_view, 4> fields;
        std::size_t count = 0;
        while (count < fields.size()) {
            const std::size_t tab = line.find('\t');
            fields[count++] = line.substr(0, tab);
            if (tab == std::string_view::npos) {
                break;
            }
            line.remove_prefix(tab + 1);
        }
        const std::optional<WirelessService::Kind> kind = count == 3 ? kindOf(fields[1]) : std::nullopt;
        if (kind && !fields[0].empty() && !fields[2].empty()) {
            services.push_back({std::string(fields[0]), *kind, std::string(fields[2])});
        }
    }
    return services;
}

}
