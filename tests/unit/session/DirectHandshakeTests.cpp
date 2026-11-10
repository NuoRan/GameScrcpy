#include "DirectHandshake.h"
#include "Handshake.h"
#include "unit/session/DatagramVectors.h"

#include "gs/protocol/Codec.h"
#include "gs/protocol/Datagram.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace std::chrono_literals;
using detail::DirectHandshake;
using State = DirectHandshake::State;

namespace {

const DirectHandshake::TimePoint t0{std::chrono::hours(1)};
const platform::UdpEndpoint server = *platform::UdpEndpoint::parse("192.168.1.5", 27183);
const platform::UdpEndpoint stranger = *platform::UdpEndpoint::parse("192.168.1.9", 27183);
const Token token{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

std::vector<std::uint8_t> datagram(std::uint8_t channel, std::uint32_t connection, std::span<const std::uint8_t> payload = {})
{
    const auto header = protocol::encodeDatagramHeader({.channel = channel, .connection = connection});
    std::vector<std::uint8_t> out(header.begin(), header.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

template <typename M>
std::vector<std::uint8_t> handshakeDatagram(const M& message, std::uint32_t connection = 0)
{
    const auto frame = protocol::encodeFrame(message);
    REQUIRE(frame.has_value());
    return datagram(protocol::kDatagramHandshakeChannel, connection, *frame);
}

protocol::ServerHello serverHello(std::uint8_t fill)
{
    protocol::ServerHello hello;
    hello.nonce.fill(fill);
    return hello;
}

std::vector<std::uint8_t> sent(std::span<const std::uint8_t> datagram)
{
    return {datagram.begin(), datagram.end()};
}

protocol::ClientHello clientHelloIn(std::span<const std::uint8_t> datagram)
{
    const auto header = protocol::decodeDatagramHeader(datagram);
    REQUIRE(header.has_value());
    CHECK(header->channel == protocol::kDatagramHandshakeChannel);
    CHECK(header->connection == 0);
    const auto message =
        protocol::decodeToServerFrame(protocol::FrameContext::Handshake, datagram.subspan(protocol::kDatagramHeaderSize));
    REQUIRE(message.has_value());
    const auto* hello = std::get_if<protocol::ClientHello>(&*message);
    REQUIRE(hello != nullptr);
    return *hello;
}

}

TEST_CASE("the direct handshake sends and accepts the golden vector's datagrams byte for byte (SPEC 10.2)")
{
    const auto vector = testing::datagramVector();
    const auto& handshake = vector.at("handshake");
    DirectHandshake direct(testing::fixedFromHex<16>(vector.at("token").get<std::string>()), "2.0.0", server, t0);

    CHECK(direct.state() == State::Knocking);
    CHECK(direct.deadline() <= t0);
    CHECK(sent(direct.poll(t0)) == testing::fromHex(handshake.at("knock").get<std::string>()));

    direct.receive(testing::fromHex(handshake.at("serverHello").get<std::string>()), server);
    CHECK(direct.state() == State::Proving);
    CHECK(sent(direct.poll(t0 + 3ms)) == testing::fromHex(handshake.at("clientHello").get<std::string>()));

    direct.receive(testing::fromHex(handshake.at("accepted").get<std::string>()), server);
    CHECK(direct.state() == State::Accepted);
    CHECK(direct.connection() == handshake.at("connection").get<std::uint32_t>());
    CHECK(direct.poll(t0 + 1h).empty());
    CHECK(direct.deadline() == DirectHandshake::TimePoint::max());
}

TEST_CASE("the direct handshake resends every 250 ms and gives up after 1.5 s")
{
    DirectHandshake direct(token, "2.0.0", server, t0);
    const auto knock = sent(direct.poll(t0));
    CHECK(knock.size() == protocol::kDatagramHeaderSize);
    CHECK(direct.deadline() == t0 + 250ms);
    CHECK(direct.poll(t0 + 249ms).empty());
    CHECK(sent(direct.poll(t0 + 250ms)) == knock);
    CHECK(direct.deadline() == t0 + 500ms);

    direct.receive(handshakeDatagram(serverHello(0x5A)), server);
    CHECK(direct.deadline() <= t0 + 300ms);
    const auto proof = clientHelloIn(direct.poll(t0 + 300ms)).proof;
    CHECK(proof == detail::udpHelloProof(token, serverHello(0x5A).nonce));
    CHECK(direct.poll(t0 + 549ms).empty());
    CHECK(clientHelloIn(direct.poll(t0 + 550ms)).proof == proof);
    CHECK(clientHelloIn(direct.poll(t0 + 1300ms)).proof == proof);
    CHECK(direct.deadline() == t0 + 1500ms);

    CHECK(direct.poll(t0 + 1499ms).empty());
    CHECK(direct.state() == State::Proving);
    CHECK(direct.poll(t0 + 1500ms).empty());
    REQUIRE(direct.state() == State::Failed);
    CHECK(direct.failure().kind == FailureKind::DirectUnreachable);
    CHECK(direct.failure().detail == "no acceptance from 192.168.1.5:27183 within 1500 ms");
    CHECK(direct.deadline() == DirectHandshake::TimePoint::max());

    DirectHandshake silent(token, "2.0.0", server, t0);
    CHECK_FALSE(silent.poll(t0).empty());
    CHECK(silent.poll(t0 + 2s).empty());
    REQUIRE(silent.state() == State::Failed);
    CHECK(silent.failure().kind == FailureKind::DirectUnreachable);
    CHECK(silent.failure().detail == "no ServerHello from 192.168.1.5:27183 within 1500 ms");
}

TEST_CASE("a repeated ServerHello keeps the ClientHello's pace; a new nonce is proven at once")
{
    DirectHandshake direct(token, "2.0.0", server, t0);
    CHECK_FALSE(direct.poll(t0).empty());
    direct.receive(handshakeDatagram(serverHello(0x11)), server);
    CHECK(clientHelloIn(direct.poll(t0 + 10ms)).proof == detail::udpHelloProof(token, serverHello(0x11).nonce));

    direct.receive(handshakeDatagram(serverHello(0x11)), server);
    CHECK(direct.poll(t0 + 20ms).empty());
    CHECK(direct.deadline() == t0 + 260ms);

    direct.receive(handshakeDatagram(serverHello(0x22)), server);
    CHECK(direct.state() == State::Proving);
    CHECK(clientHelloIn(direct.poll(t0 + 30ms)).proof == detail::udpHelloProof(token, serverHello(0x22).nonce));
    CHECK(direct.deadline() == t0 + 280ms);
}

TEST_CASE("the direct handshake ignores whatever is not a well-formed handshake datagram from the server")
{
    DirectHandshake direct(token, "2.0.0", server, t0);
    CHECK_FALSE(direct.poll(t0).empty());

    direct.receive(handshakeDatagram(serverHello(0x33)), stranger);
    auto badMagic = handshakeDatagram(serverHello(0x33));
    badMagic[0] = 'g';
    direct.receive(badMagic, server);
    direct.receive(datagram(1, 0, handshakeDatagram(serverHello(0x33))), server);
    auto trailing = handshakeDatagram(serverHello(0x33));
    trailing.push_back(0);
    direct.receive(trailing, server);
    direct.receive(datagram(protocol::kDatagramHandshakeChannel, 7), server);
    CHECK(direct.state() == State::Knocking);

    direct.receive(handshakeDatagram(serverHello(0x33)), server);
    REQUIRE(direct.state() == State::Proving);
    direct.receive(handshakeDatagram(protocol::Welcome{}, 7), server);
    direct.receive(datagram(protocol::kDatagramHandshakeChannel, 0), server);
    CHECK(direct.state() == State::Proving);

    CHECK(direct.poll(t0 + 1500ms).empty());
    REQUIRE(direct.state() == State::Failed);
    CHECK(direct.failure().kind == FailureKind::DirectUnreachable);
    CHECK(direct.failure().detail ==
          "no acceptance from 192.168.1.5:27183 within 1500 ms; ignored 7 datagrams, the last an acceptance "
          "without a connection number");

    DirectHandshake other(token, "2.0.0", server, t0);
    other.receive(handshakeDatagram(serverHello(0x33)), stranger);
    other.receive(trailing, server);
    CHECK(other.poll(t0 + 1500ms).empty());
    CHECK(other.failure().detail ==
          "no ServerHello from 192.168.1.5:27183 within 1500 ms; ignored 2 datagrams, the last a malformed handshake "
          "message (TrailingBytes)");
}

TEST_CASE("a refusal or another protocol version ends the direct handshake at once")
{
    DirectHandshake refused(token, "2.0.0", server, t0);
    CHECK_FALSE(refused.poll(t0).empty());
    refused.receive(handshakeDatagram(serverHello(0x44)), server);
    CHECK_FALSE(refused.poll(t0 + 5ms).empty());
    protocol::HandshakeError error;
    error.code = protocol::ErrorCode::AuthenticationFailed;
    error.message = "the token does not match";
    refused.receive(handshakeDatagram(error), server);
    REQUIRE(refused.state() == State::Failed);
    CHECK(refused.failure().kind == FailureKind::HandshakeRejected);
    CHECK(refused.failure().code == protocol::ErrorCode::AuthenticationFailed);
    CHECK(refused.failure().detail ==
          "the server refused the handshake (authenticationFailed): the token does not match");
    CHECK(refused.poll(t0 + 300ms).empty());
    refused.receive(datagram(protocol::kDatagramHandshakeChannel, 7), server);
    CHECK(refused.state() == State::Failed);

    DirectHandshake newer(token, "2.0.0", server, t0);
    auto hello = serverHello(0x55);
    hello.protocolVersion = protocol::kProtocolVersion + 1;
    newer.receive(handshakeDatagram(hello), server);
    REQUIRE(newer.state() == State::Failed);
    CHECK(newer.failure().kind == FailureKind::HandshakeRejected);
    CHECK(newer.failure().code == protocol::ErrorCode::VersionMismatch);
}
