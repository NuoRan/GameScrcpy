#include "JsonVisitors.h"
#include "fakes/VideoShards.h"

#include <gs/protocol/DirectMedia.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace gs::protocol;
using gs::protocol::testing::fromHex;
using gs::protocol::testing::toHex;
using nlohmann::json;

namespace {

json mediaVector()
{
    std::ifstream stream(std::filesystem::path(GS_PROTOCOL_VECTOR_DIR) / "direct" / "media.json", std::ios::binary);
    REQUIRE(stream);
    return json::parse(stream).at("vectors").at(0);
}

std::vector<std::vector<std::uint8_t>> datagramsOf(const json& list)
{
    std::vector<std::vector<std::uint8_t>> out;
    for (const json& hex : list) {
        out.push_back(fromHex(hex.get<std::string>()));
    }
    return out;
}

}

TEST_CASE("media channels open with an empty datagram (SPEC 10.5)")
{
    const json vector = mediaVector();
    for (const auto& [name, channel] : {std::pair{"video", kVideoChannel}, std::pair{"audio", kAudioChannel}}) {
        const auto bytes = fromHex(vector.at("open").at(name).get<std::string>());
        CHECK(bytes.size() == kDatagramHeaderSize);
        const auto header = decodeDatagramHeader(bytes);
        REQUIRE(header.has_value());
        CHECK(header->channel == channel);
        CHECK(header->connection == vector.at("connection").get<std::uint32_t>());
    }
    CHECK(vector.at("maxShardSize").get<std::size_t>() == kMaxShardSize);
}

TEST_CASE("\"closed\" and its acknowledgement are one byte 0 after the header (SPEC 10.5, ADR-0056)")
{
    const json vector = mediaVector();
    const auto connection = vector.at("connection").get<std::uint32_t>();
    for (const auto& [name, channel] : {std::pair{"video", kVideoChannel}, std::pair{"audio", kAudioChannel}}) {
        const auto bytes = fromHex(vector.at("closed").at(name).get<std::string>());
        const auto encoded = encodeMediaClosed(channel, connection);
        CHECK(toHex(encoded) == toHex(bytes));
        CHECK(isMediaClosed(bytes));
        auto other = bytes;
        other.back() = 1;
        CHECK_FALSE(isMediaClosed(other));
        CHECK_FALSE(isMediaClosed(fromHex(vector.at("open").at(name).get<std::string>())));
    }
}

TEST_CASE("video shards decode to their fields and put the packet back, with or without the lost ones (SPEC 10.5)")
{
    const json vector = mediaVector();
    for (const json& entry : vector.at("video")) {
        const auto name = entry.at("name").get<std::string>();
        CAPTURE(name);
        const auto packet = fromHex(entry.at("bytes").get<std::string>());
        const auto datagrams = datagramsOf(entry.at("datagrams"));
        const auto parity = entry.at("parity").get<std::vector<int>>();
        for (const auto& datagram : datagrams) {
            CHECK(datagram.size() <= kReliableDatagramLimit);
            const auto shard = decodeVideoShard(datagram);
            REQUIRE(shard.has_value());
            CHECK(shard->packet == entry.at("packet").get<std::uint32_t>());
            CHECK(shard->packetSize == packet.size());
            CHECK(shard->shardSize == entry.at("shardSize").get<std::uint16_t>());
            CHECK(shard->m == parity.at(shard->group));
        }

        VideoAssembler whole;
        for (const auto& datagram : datagrams) {
            whole.add(*decodeVideoShard(datagram));
        }
        auto delivered = whole.next();
        REQUIRE(delivered.has_value());
        CHECK(toHex(delivered->bytes) == entry.at("bytes").get<std::string>());
        CHECK(whole.stats().shardsRecovered == 0);

        for (const json& loss : entry.at("losses")) {
            const auto lost = loss.at("lost").get<std::vector<std::size_t>>();
            CAPTURE(loss.dump());
            VideoAssembler assembler;
            for (std::size_t i = 0; i < datagrams.size(); ++i) {
                if (std::ranges::find(lost, i) == lost.end()) {
                    assembler.add(*decodeVideoShard(datagrams[i]));
                }
            }
            const auto rebuilt = assembler.next();
            if (loss.at("recovered").get<bool>()) {
                REQUIRE(rebuilt.has_value());
                CHECK(toHex(rebuilt->bytes) == entry.at("bytes").get<std::string>());
            } else {
                CHECK_FALSE(rebuilt.has_value());
            }
        }
    }
}

TEST_CASE("malformed media datagrams report the error the specification orders (SPEC 10.5, 10.6)")
{
    const json vector = mediaVector();
    for (const json& entry : vector.at("videoMalformed")) {
        CAPTURE(entry.at("name").get<std::string>());
        const auto shard = decodeVideoShard(fromHex(entry.at("hex").get<std::string>()));
        REQUIRE_FALSE(shard.has_value());
        CHECK(std::string(errorName(shard.error())) == entry.at("error").get<std::string>());
    }
    for (const json& entry : vector.at("audioMalformed")) {
        CAPTURE(entry.at("name").get<std::string>());
        const auto audio = decodeAudioDatagram(fromHex(entry.at("hex").get<std::string>()));
        REQUIRE_FALSE(audio.has_value());
        CHECK(std::string(errorName(audio.error())) == entry.at("error").get<std::string>());
    }
}

TEST_CASE("audio datagrams carry the previous packet, so one lost datagram costs nothing (SPEC 10.6)")
{
    const json vector = mediaVector();
    const auto packets = datagramsOf(vector.at("audio").at("packets"));
    const auto datagrams = datagramsOf(vector.at("audio").at("datagrams"));
    for (std::size_t i = 0; i < datagrams.size(); ++i) {
        const auto audio = decodeAudioDatagram(datagrams[i]);
        REQUIRE(audio.has_value());
        CHECK(audio->packet == i);
        CHECK(audio->count == (i == 0 ? 1 : 2));
        CHECK(toHex(audio->packets[0]) == toHex(packets[i]));
        if (i > 0) {
            CHECK(toHex(audio->packets[1]) == toHex(packets[i - 1]));
        }
    }
    const auto run = [&](std::vector<std::size_t> arriving) {
        AudioAssembler assembler;
        for (const std::size_t i : arriving) {
            assembler.add(*decodeAudioDatagram(datagrams[i]));
        }
        std::vector<std::pair<std::string, std::uint32_t>> out;
        while (auto delivered = assembler.next()) {
            out.emplace_back(toHex(delivered->bytes), delivered->lostBefore);
        }
        return out;
    };
    using Out = std::vector<std::pair<std::string, std::uint32_t>>;
    CHECK(run({0, 1, 2}) == Out{{toHex(packets[0]), 0}, {toHex(packets[1]), 0}, {toHex(packets[2]), 0}});
    CHECK(run({0, 2}) == Out{{toHex(packets[0]), 0}, {toHex(packets[1]), 0}, {toHex(packets[2]), 0}});
    CHECK(run({2}) == Out{{toHex(packets[1]), 1}, {toHex(packets[2]), 0}});
    CHECK(run({0, 2, 1}) == Out{{toHex(packets[0]), 0}, {toHex(packets[1]), 0}, {toHex(packets[2]), 0}});
}

TEST_CASE("parity arriving after its group is rebuilt is not lost; what never comes counts once a later packet is whole")
{
    const std::vector<std::uint8_t> first(2500, 0x11);
    const std::vector<std::uint8_t> second(800, 0x22);
    const auto a = testing::videoShards(0, first, 1000, 2);
    const auto b = testing::videoShards(1, second, 1000, 2);

    VideoAssembler whole;
    for (const auto* datagrams : {&a, &b}) {
        for (const auto& datagram : *datagrams) {
            whole.add(*decodeVideoShard(datagram));
        }
    }
    CHECK(whole.next().has_value());
    CHECK(whole.next().has_value());
    CHECK(whole.stats().shardsLost == 0);
    CHECK(whole.stats().shardsRecovered == 0);

    VideoAssembler lossy;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i != 1 && i != 4) {
            lossy.add(*decodeVideoShard(a[i]));
        }
    }
    CHECK(lossy.next().has_value());
    CHECK(lossy.stats().shardsRecovered == 1);
    CHECK(lossy.stats().shardsLost == 0);
    lossy.add(*decodeVideoShard(b[0]));
    CHECK(lossy.next().has_value());
    CHECK(lossy.stats().shardsLost == 2);
}

TEST_CASE("under random shard loss every packet handed out is the one sent, and the rest are counted lost")
{
    std::mt19937 random(5);
    std::vector<std::vector<std::uint8_t>> sent;
    VideoAssembler assembler;
    std::size_t delivered = 0;
    std::uint64_t lostBefore = 0;
    bool intact = true;
    for (std::uint32_t number = 0; number < 300; ++number) {
        std::vector<std::uint8_t> packet(std::uniform_int_distribution<std::size_t>(20, 40'000)(random));
        for (auto& byte : packet) {
            byte = static_cast<std::uint8_t>(random());
        }
        auto datagrams = testing::videoShards(
            number, packet, static_cast<std::uint16_t>(std::min<std::size_t>(kMaxShardSize, 1 + packet.size() / 3)), 3);
        std::shuffle(datagrams.begin(), datagrams.end(), random);
        for (const auto& datagram : datagrams) {
            if (std::uniform_real_distribution<double>(0, 1)(random) >= 0.06) {
                assembler.add(*decodeVideoShard(datagram));
            }
        }
        sent.push_back(std::move(packet));
        while (auto out = assembler.next()) {
            lostBefore += out->lostBefore;
            const std::size_t at = delivered + lostBefore;
            intact = intact && at < sent.size() && out->bytes == sent[at];
            ++delivered;
        }
    }
    CHECK(intact);
    CHECK(delivered > 250);
    CHECK(delivered + lostBefore <= sent.size());
    CHECK(sent.size() - delivered - lostBefore <= VideoAssembler::kMaxPending);
    CHECK(assembler.stats().shardsRecovered > 0);
    CHECK(assembler.stats().packetsLost == lostBefore);
}
