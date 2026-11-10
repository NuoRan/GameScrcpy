#pragma once

#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace gs::esp32 {

inline constexpr std::wstring_view kMdnsService = L"_gs-esp32._udp.local";

struct FoundBoard {
    std::string instance;
    std::string host;
    std::string address;
    std::uint16_t port = 0;
    std::string id;
    std::string name;
    std::string chip;

    friend bool operator==(const FoundBoard&, const FoundBoard&) = default;
};

[[nodiscard]] std::vector<FoundBoard> browseBoards(std::chrono::milliseconds duration, const std::stop_token& stop = {});

}
