#include "JsonVisitors.h"

#include <gs/protocol/ReedSolomon.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using gs::protocol::ReedSolomon;
using gs::protocol::testing::fromHex;
using gs::protocol::testing::toHex;
using nlohmann::json;

namespace {

using Shards = std::vector<std::vector<std::uint8_t>>;

std::vector<std::uint8_t*> pointers(Shards& shards)
{
    std::vector<std::uint8_t*> result;
    for (auto& shard : shards) {
        result.push_back(shard.data());
    }
    return result;
}

std::bitset<ReedSolomon::kMaxShards> allOf(int count)
{
    std::bitset<ReedSolomon::kMaxShards> present;
    for (int i = 0; i < count; ++i) {
        present.set(static_cast<std::size_t>(i));
    }
    return present;
}

}

TEST_CASE("Reed-Solomon golden vectors: the parity byte for byte, the data back from every listed loss")
{
    std::ifstream stream(std::filesystem::path(GS_PROTOCOL_VECTOR_DIR) / "fec" / "reed-solomon.json", std::ios::binary);
    REQUIRE(stream);
    const json document = json::parse(stream);
    REQUIRE(document.at("vectors").size() >= 10);
    for (const json& vector : document.at("vectors")) {
        const auto name = vector.at("name").get<std::string>();
        CAPTURE(name);
        const int k = vector.at("k").get<int>();
        const int m = vector.at("m").get<int>();
        Shards shards;
        for (const json& hex : vector.at("data")) {
            shards.push_back(fromHex(hex.get<std::string>()));
        }
        REQUIRE(shards.size() == static_cast<std::size_t>(k));
        const std::size_t length = shards.front().size();
        shards.resize(static_cast<std::size_t>(k + m), std::vector<std::uint8_t>(length, 0xAA));

        const ReedSolomon code(k, m);
        code.encode(pointers(shards), length);
        for (int i = 0; i < m; ++i) {
            CHECK(toHex(shards[static_cast<std::size_t>(k + i)]) == vector.at("parity").at(i).get<std::string>());
        }

        for (const json& lost : vector.at("erasures")) {
            Shards damaged = shards;
            auto present = allOf(k + m);
            for (const json& index : lost) {
                const auto at = index.get<std::size_t>();
                present.reset(at);
                std::ranges::fill(damaged[at], std::uint8_t{0x5C});
            }
            REQUIRE(code.reconstruct(pointers(damaged), present, length));
            for (int d = 0; d < k; ++d) {
                CHECK(damaged[static_cast<std::size_t>(d)] == shards[static_cast<std::size_t>(d)]);
            }
        }
    }
}

TEST_CASE("Reed-Solomon recovers any loss up to its parity count and refuses a greater one")
{
    std::mt19937 random(2026);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int round = 0; round < 300; ++round) {
        const int k = std::uniform_int_distribution<int>(1, 64)(random);
        const int m = std::uniform_int_distribution<int>(0, 16)(random);
        const auto length = static_cast<std::size_t>(std::uniform_int_distribution<int>(1, 300)(random));
        CAPTURE(k);
        CAPTURE(m);
        Shards shards(static_cast<std::size_t>(k + m), std::vector<std::uint8_t>(length));
        for (int d = 0; d < k; ++d) {
            std::ranges::generate(shards[static_cast<std::size_t>(d)],
                                  [&] { return static_cast<std::uint8_t>(byte(random)); });
        }
        const ReedSolomon code(k, m);
        code.encode(pointers(shards), length);

        std::vector<std::size_t> order(static_cast<std::size_t>(k + m));
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::shuffle(order, random);
        const auto lostCount = static_cast<std::size_t>(std::uniform_int_distribution<int>(0, m)(random));
        Shards damaged = shards;
        auto present = allOf(k + m);
        for (std::size_t i = 0; i < lostCount; ++i) {
            present.reset(order[i]);
            std::ranges::fill(damaged[order[i]], std::uint8_t{0});
        }
        REQUIRE(code.reconstruct(pointers(damaged), present, length));
        for (int d = 0; d < k; ++d) {
            REQUIRE(damaged[static_cast<std::size_t>(d)] == shards[static_cast<std::size_t>(d)]);
        }

        auto tooFew = allOf(k + m);
        for (std::size_t i = 0; i <= static_cast<std::size_t>(m); ++i) {
            tooFew.reset(order[i]);
        }
        Shards spare = shards;
        CHECK_FALSE(code.reconstruct(pointers(spare), tooFew, length));
    }
}
