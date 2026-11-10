#pragma once

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gs::session::testing {

inline nlohmann::json directVector(const char* file)
{
    std::ifstream stream(std::filesystem::path(GS_PROTOCOL_VECTOR_DIR) / "direct" / file, std::ios::binary);
    REQUIRE(stream);
    return nlohmann::json::parse(stream).at("vectors").at(0);
}

inline nlohmann::json datagramVector()
{
    return directVector("datagrams.json");
}

inline nlohmann::json reliableVector()
{
    return directVector("reliable.json");
}

inline std::vector<std::uint8_t> fromHex(const std::string& hex)
{
    std::vector<std::uint8_t> bytes(hex.size() / 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(std::stoi(hex.substr(2 * i, 2), nullptr, 16));
    }
    return bytes;
}

template <std::size_t N>
std::array<std::uint8_t, N> fixedFromHex(const std::string& hex)
{
    const auto bytes = fromHex(hex);
    REQUIRE(bytes.size() == N);
    std::array<std::uint8_t, N> out{};
    std::ranges::copy(bytes, out.begin());
    return out;
}

}
