#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gs::adb {

struct WirelessService {
    enum class Kind {
        Pairing,
        Connect,
    };

    std::string name;
    Kind kind = Kind::Pairing;
    std::string address;

    friend bool operator==(const WirelessService&, const WirelessService&) = default;
};

[[nodiscard]] std::vector<WirelessService> parseWirelessServices(std::string_view text);

}
