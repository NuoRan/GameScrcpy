#include "Handshake.h"
#include "unit/session/DatagramVectors.h"

#include "gs/platform/Crypto.h"
#include "gs/protocol/Reliable.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <numeric>
#include <string>

using namespace gs;
using namespace gs::session;
using testing::fixedFromHex;
using testing::fromHex;

TEST_CASE("the UDP handshake proof and the control tags match the golden vectors (SPEC 10.2)")
{
    const auto vector = testing::datagramVector();
    const auto token = fixedFromHex<16>(vector.at("token").get<std::string>());
    const auto nonce = fixedFromHex<16>(vector.at("nonce").get<std::string>());

    CHECK(platform::toHex(detail::udpHelloProof(token, nonce)) == vector.at("helloProof").get<std::string>());
    CHECK(platform::toHex(detail::helloProof(token, nonce)) == vector.at("tcpHelloProof").get<std::string>());
    for (const auto& entry : vector.at("controlTags")) {
        CAPTURE(entry.at("name").get<std::string>());
        const auto body = fromHex(entry.at("body").get<std::string>());
        CHECK(platform::toHex(detail::controlTag(token, body)) == entry.at("tag").get<std::string>());
    }
}

TEST_CASE("the reliable channel's control datagrams end with the tag over everything before it (SPEC 10.3)")
{
    const auto vector = testing::reliableVector();
    const auto token = fixedFromHex<16>(vector.at("token").get<std::string>());
    const auto tagFits = [&token](const std::vector<std::uint8_t>& datagram) {
        const auto parts = protocol::splitControlDatagram(datagram);
        REQUIRE(parts.has_value());
        return platform::toHex(detail::controlTag(token, parts->authenticated)) == platform::toHex(parts->tag);
    };
    for (const auto& entry : vector.at("datagrams")) {
        if (entry.at("channel").get<int>() == protocol::kControlChannel) {
            CAPTURE(entry.at("name").get<std::string>());
            CHECK(tagFits(fromHex(entry.at("hex").get<std::string>())));
        }
    }
    for (const auto& entry : vector.at("malformed")) {
        const auto name = entry.at("name").get<std::string>();
        CAPTURE(name);
        if (entry.at("channel").get<int>() != protocol::kControlChannel || name == "control-short") {
            continue;
        }
        CHECK(tagFits(fromHex(entry.at("hex").get<std::string>())) == (entry.at("error") != "BadTag"));
    }
}

TEST_CASE("handshake proofs match the reference HMAC that the device server accepts")
{
    Token token{};
    std::iota(token.begin(), token.end(), std::uint8_t{0x00});
    protocol::Nonce nonce{};
    std::iota(nonce.begin(), nonce.end(), std::uint8_t{0x10});

    CHECK(platform::toHex(detail::helloProof(token, nonce)) ==
          "f60ad39d2e36645efe58ed945d8d7ac1c3d6365f721d912925fca68d0c45ad44");
    CHECK(platform::toHex(detail::channelProof(token, nonce, protocol::ChannelId::Video)) ==
          "dc93fe5f1677ff1b5ad6bac80b7daf9d5cec01c0884891bbf5f61c2b007aeb97");
}
