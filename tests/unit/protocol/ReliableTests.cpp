#include "JsonVisitors.h"

#include <gs/protocol/Reliable.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace gs::protocol;
using gs::protocol::testing::fromHex;
using gs::protocol::testing::toHex;
using nlohmann::json;

namespace {

json reliableVector()
{
    std::ifstream stream(std::filesystem::path(GS_PROTOCOL_VECTOR_DIR) / "direct" / "reliable.json", std::ios::binary);
    REQUIRE(stream);
    return json::parse(stream).at("vectors").at(0);
}

void checkBlock(const ReliableBlock& block, const json& entry)
{
    CHECK(block.ack == entry.at("ack").get<std::uint32_t>());
    CHECK(block.sack == entry.at("sack").get<std::uint32_t>());
    const json& segments = entry.at("segments");
    REQUIRE(block.count == segments.size());
    SegmentReader reader(block);
    for (const json& expected : segments) {
        const auto segment = reader.next();
        REQUIRE(segment.has_value());
        CHECK(segment->seq == expected.at("seq").get<std::uint32_t>());
        CHECK(toHex(segment->bytes) == expected.at("hex").get<std::string>());
    }
    CHECK_FALSE(reader.next().has_value());
}

}

TEST_CASE("reliable channel datagrams decode to their fields and encode byte for byte (SPEC 10.3)")
{
    const json vector = reliableVector();
    CHECK(vector.at("maxSegment").at("control").get<std::size_t>() == kMaxControlSegment);
    CHECK(vector.at("maxSegment").at("bulk").get<std::size_t>() == kMaxBulkSegment);
    const auto connection = vector.at("connection").get<std::uint32_t>();

    for (const json& entry : vector.at("datagrams")) {
        const auto name = entry.at("name").get<std::string>();
        CAPTURE(name);
        const auto bytes = fromHex(entry.at("hex").get<std::string>());
        CHECK(bytes.size() <= kReliableDatagramLimit);
        const auto header = decodeDatagramHeader(bytes);
        REQUIRE(header.has_value());
        CHECK(header->channel == entry.at("channel").get<std::uint8_t>());
        CHECK(header->connection == connection);

        std::array<std::uint8_t, kReliableDatagramLimit> out{};
        if (header->channel == kControlChannel) {
            const auto parts = splitControlDatagram(bytes);
            REQUIRE(parts.has_value());
            const auto body = decodeControlBody(parts->authenticated);
            REQUIRE(body.has_value());
            CHECK(body->counter == std::stoull(entry.at("counter").get<std::string>()));
            checkBlock(body->block, entry);

            auto writer = ReliableWriter::control(out, connection, body->counter, body->block.ack, body->block.sack);
            SegmentReader reader(body->block);
            while (const auto segment = reader.next()) {
                REQUIRE(writer.add(segment->seq, segment->bytes));
            }
            CHECK(toHex(writer.finish()) == toHex(parts->authenticated));
            std::array<std::uint8_t, kControlTagSize> tag{};
            std::ranges::copy(parts->tag, tag.begin());
            CHECK(toHex(writer.seal(tag)) == entry.at("hex").get<std::string>());
        } else {
            REQUIRE(header->channel == kBulkChannel);
            const auto block = decodeBulkDatagram(bytes);
            REQUIRE(block.has_value());
            checkBlock(*block, entry);

            auto writer = ReliableWriter::bulk(out, connection, block->ack, block->sack);
            SegmentReader reader(*block);
            while (const auto segment = reader.next()) {
                REQUIRE(writer.add(segment->seq, segment->bytes));
            }
            CHECK(toHex(writer.finish()) == entry.at("hex").get<std::string>());
        }
    }
}

TEST_CASE("malformed reliable channel datagrams report the error the specification orders")
{
    const json vector = reliableVector();
    for (const json& entry : vector.at("malformed")) {
        const auto name = entry.at("name").get<std::string>();
        const auto expected = entry.at("error").get<std::string>();
        CAPTURE(name);
        if (expected == "BadTag") {
            continue;
        }
        const auto bytes = fromHex(entry.at("hex").get<std::string>());
        DecodeError error = DecodeError::Truncated;
        if (entry.at("channel").get<int>() == kControlChannel) {
            const auto parts = splitControlDatagram(bytes);
            if (!parts) {
                error = parts.error();
            } else {
                const auto body = decodeControlBody(parts->authenticated);
                REQUIRE_FALSE(body.has_value());
                error = body.error();
            }
        } else {
            const auto block = decodeBulkDatagram(bytes);
            REQUIRE_FALSE(block.has_value());
            error = block.error();
        }
        CHECK(std::string(errorName(error)) == expected);
    }
}

TEST_CASE("the reliable writer keeps datagrams within 1200 bytes")
{
    std::array<std::uint8_t, kReliableDatagramLimit> out{};
    const std::vector<std::uint8_t> big(kMaxBulkSegment + 1, 0xAB);

    auto control = ReliableWriter::control(out, 7, 1, 0, 0);
    CHECK(control.room() == kMaxControlSegment);
    CHECK_FALSE(control.add(0, std::span(big).first(kMaxControlSegment + 1)));
    CHECK_FALSE(control.add(0, {}));
    CHECK(control.add(0, std::span(big).first(100)));
    CHECK(control.room() == kMaxControlSegment - 100 - kSegmentHeaderSize);
    CHECK(control.add(1, std::span(big).first(control.room())));
    CHECK(control.room() == 0);
    CHECK(control.count() == 2);
    CHECK(control.finish().size() == kReliableDatagramLimit - kControlTagSize);

    auto bulk = ReliableWriter::bulk(out, 7, 0, 0);
    CHECK(bulk.room() == kMaxBulkSegment);
    CHECK_FALSE(bulk.add(0, big));
    CHECK(bulk.add(0, std::span(big).first(kMaxBulkSegment)));
    CHECK(bulk.room() == 0);
    CHECK(bulk.finish().size() == kReliableDatagramLimit);
}
