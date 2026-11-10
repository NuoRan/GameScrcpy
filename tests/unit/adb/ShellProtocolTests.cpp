#include "gs/adb/ShellProtocol.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace gs::adb;

namespace {

std::vector<std::uint8_t> packet(std::uint8_t id, std::string_view data)
{
    std::vector<std::uint8_t> bytes{id};
    const auto length = static_cast<std::uint32_t>(data.size());
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(length >> shift));
    }
    bytes.insert(bytes.end(), data.begin(), data.end());
    return bytes;
}

std::string text(const ShellPacket& p)
{
    return {p.data.begin(), p.data.end()};
}

}

TEST_CASE("shell packets: the bytes captured from adbd")
{
    const std::vector<std::uint8_t> captured{1, 4, 0, 0, 0, 'o', 'u', 't', '\n', 2, 4, 0, 0, 0,
                                             'e', 'r', 'r', '\n', 3, 1, 0, 0, 0, 3};
    ShellPacketParser parser;
    parser.feed(captured);
    auto out = parser.next();
    auto err = parser.next();
    auto exit = parser.next();
    REQUIRE(out);
    REQUIRE(err);
    REQUIRE(exit);
    CHECK(out->id == 1);
    CHECK(text(*out) == "out\n");
    CHECK(err->id == 2);
    CHECK(text(*err) == "err\n");
    CHECK(exit->id == 3);
    CHECK(exit->data == std::vector<std::uint8_t>{3});
    CHECK_FALSE(parser.next());
    CHECK_FALSE(parser.failed());
}

TEST_CASE("shell packets: reassembled from single-byte reads")
{
    std::vector<std::uint8_t> stream = packet(1, "hello");
    const std::vector<std::uint8_t> empty = packet(2, "");
    stream.insert(stream.end(), empty.begin(), empty.end());
    const std::string big(70000, 'x');
    const std::vector<std::uint8_t> large = packet(1, big);
    stream.insert(stream.end(), large.begin(), large.end());

    ShellPacketParser parser;
    std::vector<ShellPacket> packets;
    for (const std::uint8_t byte : stream) {
        parser.feed(std::span(&byte, 1));
        while (auto p = parser.next()) {
            packets.push_back(std::move(*p));
        }
    }
    REQUIRE(packets.size() == 3);
    CHECK(text(packets[0]) == "hello");
    CHECK(packets[1].id == 2);
    CHECK(packets[1].data.empty());
    CHECK(packets[2].data.size() == big.size());
}

TEST_CASE("shell packets: an oversized length is a protocol error")
{
    ShellPacketParser parser;
    const std::vector<std::uint8_t> header{1, 0xFF, 0xFF, 0xFF, 0x7F};
    parser.feed(header);
    CHECK_FALSE(parser.next());
    CHECK(parser.failed());
    parser.feed(packet(1, "late"));
    CHECK_FALSE(parser.next());
}

TEST_CASE("shell quoting")
{
    CHECK(shellQuote("plain") == "'plain'");
    CHECK(shellQuote("") == "''");
    CHECK(shellQuote("a b;c$d") == "'a b;c$d'");
    CHECK(shellQuote("it's") == "'it'\\''s'");
}
