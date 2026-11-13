#include "gs/platform/UdpSocket.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

using namespace gs::platform;
using namespace std::chrono_literals;

namespace {

std::vector<std::uint8_t> bytes(std::string_view text)
{
    return {text.begin(), text.end()};
}

UdpSocket boundLoopback()
{
    auto socket = UdpSocket::bind(UdpEndpoint::loopback(0));
    REQUIRE(socket.has_value());
    REQUIRE(socket->localPort() != 0);
    return std::move(*socket);
}

std::string_view markingName(UdpSocket::Marking marking)
{
    switch (marking) {
    case UdpSocket::Marking::None:
        return "none";
    case UdpSocket::Marking::Pending:
        return "pending";
    case UdpSocket::Marking::Video:
        return "video";
    case UdpSocket::Marking::Failed:
        return "failed";
    }
    return "none";
}

UdpSocket::Marking settledMarking(const UdpSocket& socket)
{
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (socket.marking() == UdpSocket::Marking::Pending &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    return socket.marking();
}

void roundTrip(UdpSocket& from, UdpSocket& to)
{
    from.setReceiveTimeout(2s);
    to.setReceiveTimeout(2s);
    std::array<std::uint8_t, 64> buffer{};
    const auto request = bytes("marked");
    REQUIRE(from.sendTo(UdpEndpoint::loopback(to.localPort()), request));
    const auto received = to.receive(buffer);
    REQUIRE(received.has_value());
    CHECK(received->size == request.size());
    CHECK(received->from == UdpEndpoint::loopback(from.localPort()));
    const auto reply = bytes("still flowing");
    REQUIRE(to.sendTo(received->from, reply));
    const auto answered = from.receive(buffer);
    REQUIRE(answered.has_value());
    CHECK(answered->size == reply.size());
}

}

TEST_CASE("endpoints parse and print both families")
{
    const auto v4 = UdpEndpoint::parse("192.168.1.5", 5556);
    REQUIRE(v4.has_value());
    CHECK_FALSE(v4->v6);
    CHECK(v4->address[0] == 192);
    CHECK(v4->address[3] == 5);
    CHECK(v4->toString() == "192.168.1.5:5556");

    const auto v6 = UdpEndpoint::parse("fe80::1", 7);
    REQUIRE(v6.has_value());
    CHECK(v6->v6);
    CHECK(v6->address[0] == 0xFE);
    CHECK(v6->address[15] == 1);
    CHECK(v6->toString() == "[fe80::1]:7");

    CHECK_FALSE(UdpEndpoint::parse("", 1));
    CHECK_FALSE(UdpEndpoint::parse("phone.local", 1));
    CHECK_FALSE(UdpEndpoint::parse("192.168.1.300", 1));
    CHECK(UdpEndpoint::loopback(9).toString() == "127.0.0.1:9");
    CHECK(UdpEndpoint::any(false, 0).toString() == "0.0.0.0:0");
    CHECK(UdpEndpoint::loopback(9) == UdpEndpoint::loopback(9));
}

TEST_CASE("datagrams go both ways between two sockets, each told the other's address")
{
    UdpSocket a = boundLoopback();
    UdpSocket b = boundLoopback();
    a.setReceiveTimeout(2s);
    b.setReceiveTimeout(2s);

    const auto request = bytes("knock");
    REQUIRE(a.sendTo(UdpEndpoint::loopback(b.localPort()), request));
    std::array<std::uint8_t, 2048> buffer{};
    const auto received = b.receive(buffer);
    REQUIRE(received.has_value());
    CHECK(received->size == request.size());
    CHECK(std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + 5) == request);
    CHECK(received->from == UdpEndpoint::loopback(a.localPort()));

    const auto reply = bytes("hello back");
    REQUIRE(b.sendTo(received->from, reply));
    const auto answered = a.receive(buffer);
    REQUIRE(answered.has_value());
    CHECK(answered->size == reply.size());
    CHECK(answered->from.port == b.localPort());

    SUBCASE("an empty datagram arrives as such")
    {
        REQUIRE(a.sendTo(UdpEndpoint::loopback(b.localPort()), {}));
        const auto empty = b.receive(buffer);
        REQUIRE(empty.has_value());
        CHECK(empty->size == 0);
    }

    SUBCASE("a datagram larger than the buffer is dropped, the next one delivered")
    {
        const std::vector<std::uint8_t> big(3000, 0xAB);
        REQUIRE(a.sendTo(UdpEndpoint::loopback(b.localPort()), big));
        REQUIRE(a.sendTo(UdpEndpoint::loopback(b.localPort()), bytes("small")));
        const auto next = b.receive(buffer);
        REQUIRE(next.has_value());
        CHECK(next->size == 5);
    }
}

TEST_CASE("a receive runs out of time and says so")
{
    UdpSocket socket = boundLoopback();
    socket.setReceiveTimeout(50ms);
    std::array<std::uint8_t, 64> buffer{};
    const auto started = std::chrono::steady_clock::now();
    CHECK_FALSE(socket.receive(buffer).has_value());
    CHECK(socket.receiveTimedOut());
    CHECK(std::chrono::steady_clock::now() - started < 2s);
}

TEST_CASE("interrupt wakes a blocked receive and fails every later call")
{
    UdpSocket socket = boundLoopback();
    std::optional<UdpSocket::Received> result;
    bool timedOut = true;
    std::thread receiver([&] {
        std::array<std::uint8_t, 64> buffer{};
        result = socket.receive(buffer);
        timedOut = socket.receiveTimedOut();
    });
    std::this_thread::sleep_for(100ms); // source-rule-allow R9: the receiver must be blocked before the interrupt.
    socket.interrupt();
    receiver.join();
    CHECK_FALSE(result.has_value());
    CHECK_FALSE(timedOut);

    std::array<std::uint8_t, 64> buffer{};
    CHECK_FALSE(socket.receive(buffer).has_value());
    CHECK_FALSE(socket.sendTo(UdpEndpoint::loopback(1), bytes("x")));
}

TEST_CASE("an enlarged receive buffer keeps a burst the receiver has not read yet")
{
    constexpr std::size_t kCount = 800;
    const std::vector<std::uint8_t> datagram(1200, 0x5A);
    const auto keptOfBurst = [&](UdpSocket& receiver) {
        auto sender = UdpSocket::bind(UdpEndpoint::loopback(0));
        REQUIRE(sender.has_value());
        for (std::size_t i = 0; i < kCount; ++i) {
            static_cast<void>(sender->sendTo(UdpEndpoint::loopback(receiver.localPort()), datagram));
        }
        receiver.setReceiveTimeout(200ms);
        std::array<std::uint8_t, 1500> buffer{};
        std::size_t kept = 0;
        while (receiver.receive(buffer)) {
            ++kept;
        }
        return kept;
    };

    auto plain = UdpSocket::bind(UdpEndpoint::loopback(0));
    REQUIRE(plain.has_value());
    MESSAGE(std::format("the default receive buffer kept {} of {}", keptOfBurst(*plain), kCount));

    auto enlarged = UdpSocket::bind(UdpEndpoint::loopback(0));
    REQUIRE(enlarged.has_value());
    REQUIRE(enlarged->setReceiveBufferSize(std::size_t{4} << 20));
    CHECK(keptOfBurst(*enlarged) == kCount);
}

TEST_CASE("a socket of one family does not send to the other")
{
    UdpSocket v4 = boundLoopback();
    const auto v6 = UdpEndpoint::parse("::1", 5556);
    REQUIRE(v6.has_value());
    CHECK_FALSE(v4.sendTo(*v6, bytes("x")));
}

TEST_CASE("ADR-0114: a socket marked as video traffic still sends and receives, also after a move")
{
    UdpSocket a = boundLoopback();
    UdpSocket b = boundLoopback();
    CHECK(a.marking() == UdpSocket::Marking::None);
    const bool requested = a.markAsVideo(UdpEndpoint::loopback(b.localPort()));
    const UdpSocket::Marking settled = settledMarking(a);
    MESSAGE(std::format("marking requested: {}, settled as {}", requested, markingName(settled)));
    CHECK(settled != UdpSocket::Marking::Pending);
    CHECK((settled == UdpSocket::Marking::None) == !requested);
    roundTrip(a, b);
    roundTrip(b, a);

    UdpSocket moved = std::move(a);
    CHECK(moved.marking() == settled);
    roundTrip(moved, b);
}

TEST_CASE("ADR-0114: a marking that cannot start leaves the socket as it was")
{
    UdpSocket a = boundLoopback();
    UdpSocket b = boundLoopback();
    const auto v6 = UdpEndpoint::parse("::1", b.localPort());
    REQUIRE(v6.has_value());
    CHECK_FALSE(a.markAsVideo(*v6));
    CHECK(a.marking() == UdpSocket::Marking::None);
    roundTrip(a, b);

    UdpSocket empty;
    CHECK_FALSE(empty.markAsVideo(UdpEndpoint::loopback(b.localPort())));
    CHECK(empty.marking() == UdpSocket::Marking::None);

    if (a.markAsVideo(UdpEndpoint::loopback(b.localPort()))) {
        CHECK_FALSE(a.markAsVideo(UdpEndpoint::loopback(b.localPort())));
    }
    MESSAGE(std::format("the first marking settled as {}", markingName(settledMarking(a))));
    roundTrip(a, b);
}

TEST_CASE("ADR-0114: a socket closed while its marking may still be pending closes cleanly")
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        UdpSocket closing = boundLoopback();
        static_cast<void>(closing.markAsVideo(UdpEndpoint::loopback(9)));
    }
    UdpSocket a = boundLoopback();
    UdpSocket b = boundLoopback();
    roundTrip(a, b);
    roundTrip(b, a);
}
