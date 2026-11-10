#include "JsonVisitors.h"

#include <gs/protocol/Codec.h>
#include <gs/protocol/Datagram.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

using namespace gs::protocol;
using gs::protocol::testing::fromHex;
using gs::protocol::testing::toHex;
using nlohmann::json;

namespace {

json datagramVector()
{
    std::ifstream stream(std::filesystem::path(GS_PROTOCOL_VECTOR_DIR) / "direct" / "datagrams.json", std::ios::binary);
    REQUIRE(stream);
    return json::parse(stream).at("vectors").at(0);
}

}

TEST_CASE("datagram headers encode byte for byte and decode to the same fields (SPEC 10.2)")
{
    const json vector = datagramVector();
    for (const json& entry : vector.at("headers")) {
        const auto name = entry.at("name").get<std::string>();
        CAPTURE(name);
        const auto bytes = fromHex(entry.at("hex").get<std::string>());
        const auto header = decodeDatagramHeader(bytes);
        REQUIRE(header.has_value());
        CHECK(header->channel == entry.at("channel").get<std::uint8_t>());
        CHECK(header->connection == entry.at("connection").get<std::uint32_t>());
        CHECK(bytes.size() - kDatagramHeaderSize == entry.at("payloadSize").get<std::size_t>());
        const auto encoded = encodeDatagramHeader(*header);
        CHECK(toHex(encoded) == toHex(std::span(bytes).first(kDatagramHeaderSize)));
    }
}

TEST_CASE("malformed datagram headers report the error the specification orders")
{
    const json vector = datagramVector();
    for (const json& entry : vector.at("malformed")) {
        const auto name = entry.at("name").get<std::string>();
        CAPTURE(name);
        const auto bytes = fromHex(entry.at("hex").get<std::string>());
        const auto header = decodeDatagramHeader(bytes);
        REQUIRE_FALSE(header.has_value());
        CHECK(std::string(errorName(header.error())) == entry.at("error").get<std::string>());
    }
}

TEST_CASE("the handshake datagrams carry whole handshake frames")
{
    const json vector = datagramVector();
    const json handshake = vector.at("handshake");

    const auto knock = fromHex(handshake.at("knock").get<std::string>());
    const auto knockHeader = decodeDatagramHeader(knock);
    REQUIRE(knockHeader.has_value());
    CHECK(knockHeader->channel == kDatagramHandshakeChannel);
    CHECK(knockHeader->connection == 0);
    CHECK(knock.size() == kDatagramHeaderSize);

    const auto serverHello = fromHex(handshake.at("serverHello").get<std::string>());
    REQUIRE(decodeDatagramHeader(serverHello).has_value());
    const auto greeting =
        decodeToClientFrame(FrameContext::Handshake, std::span(serverHello).subspan(kDatagramHeaderSize));
    REQUIRE(greeting.has_value());
    const auto* hello = std::get_if<ServerHello>(&*greeting);
    REQUIRE(hello != nullptr);
    CHECK(hello->protocolVersion == kProtocolVersion);
    CHECK(toHex(hello->nonce) == vector.at("nonce").get<std::string>());

    const auto clientHello = fromHex(handshake.at("clientHello").get<std::string>());
    REQUIRE(decodeDatagramHeader(clientHello).has_value());
    const auto reply = decodeToServerFrame(FrameContext::Handshake, std::span(clientHello).subspan(kDatagramHeaderSize));
    REQUIRE(reply.has_value());
    const auto* client = std::get_if<ClientHello>(&*reply);
    REQUIRE(client != nullptr);
    CHECK(client->clientVersion == "2.0.0");
    CHECK(toHex(client->proof) == vector.at("helloProof").get<std::string>());
    CHECK(vector.at("helloProof").get<std::string>() != vector.at("tcpHelloProof").get<std::string>());

    const auto accepted = fromHex(handshake.at("accepted").get<std::string>());
    const auto acceptance = decodeDatagramHeader(accepted);
    REQUIRE(acceptance.has_value());
    CHECK(acceptance->channel == kDatagramHandshakeChannel);
    CHECK(acceptance->connection == handshake.at("connection").get<std::uint32_t>());
    CHECK(acceptance->connection != 0);
    CHECK(accepted.size() == kDatagramHeaderSize);
}
