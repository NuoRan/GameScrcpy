#include "DirectTransport.h"
#include "FallbackTransport.h"
#include "Handshake.h"
#include "ReliableStream.h"
#include "fakes/VideoShards.h"

#include "gs/platform/UdpSocket.h"
#include "gs/protocol/Codec.h"
#include "gs/protocol/Datagram.h"
#include "gs/protocol/Reliable.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <stop_token>
#include <thread>
#include <variant>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace std::chrono_literals;
using detail::ReliableStream;
using SteadyClock = std::chrono::steady_clock;

namespace {

const Token token{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3};
constexpr std::uint32_t kConnection = 0x13572468;
constexpr std::size_t kLargeScreenshotBytes = std::size_t{9} << 20;

std::vector<std::uint8_t> handshakeDatagram(std::uint32_t connection, std::span<const std::uint8_t> frame = {})
{
    const auto header = protocol::encodeDatagramHeader(
        {.channel = protocol::kDatagramHandshakeChannel, .connection = connection});
    std::vector<std::uint8_t> out(header.begin(), header.end());
    out.insert(out.end(), frame.begin(), frame.end());
    return out;
}

template <typename M>
std::vector<std::uint8_t> frameOf(const M& message)
{
    return *protocol::encodeFrame(message);
}

class FakeUdpServer
{
public:
    explicit FakeUdpServer(double loss = 0, bool closeVideo = false) : m_loss(loss), m_closeVideo(closeVideo)
    {
        m_thread = std::jthread([this](std::stop_token stop) { run(stop); });
    }

    ~FakeUdpServer()
    {
        m_thread.request_stop();
        m_socket.interrupt();
    }

    FakeUdpServer(const FakeUdpServer&) = delete;
    FakeUdpServer& operator=(const FakeUdpServer&) = delete;

    [[nodiscard]] platform::UdpEndpoint endpoint() const { return platform::UdpEndpoint::loopback(m_socket.localPort()); }
    [[nodiscard]] bool saidGoodbye() const { return m_goodbye.load(); }
    [[nodiscard]] int closedAcknowledgements() const { return m_closedAcknowledgements.load(); }

private:
    void run(const std::stop_token& stop)
    {
        std::array<std::uint8_t, protocol::kDatagramMaxSize + 1> buffer{};
        while (!stop.stop_requested()) {
            const auto now = SteadyClock::now();
            if (m_control) {
                for (auto datagram = m_control->poll(now); !datagram.empty(); datagram = m_control->poll(now)) {
                    if (!lose()) {
                        static_cast<void>(m_socket.sendTo(m_peer, datagram));
                    }
                }
            }
            if (m_bulk) {
                for (auto datagram = m_bulk->poll(now); !datagram.empty(); datagram = m_bulk->poll(now)) {
                    static_cast<void>(m_socket.sendTo(m_peer, datagram));
                }
            }
            const auto wake = std::min(m_control ? m_control->deadline() : SteadyClock::time_point::max(),
                                       m_bulk ? m_bulk->deadline() : SteadyClock::time_point::max());
            const auto wait = wake == SteadyClock::time_point::max()
                                  ? 50ms
                                  : std::clamp(std::chrono::ceil<std::chrono::milliseconds>(wake - now), 1ms, 50ms);
            m_socket.setReceiveTimeout(wait);
            const auto received = m_socket.receive(buffer);
            if (!received) {
                if (m_socket.receiveTimedOut()) {
                    continue;
                }
                return;
            }
            const auto datagram = std::span<const std::uint8_t>(buffer).first(received->size);
            const auto header = protocol::decodeDatagramHeader(datagram);
            if (!header) {
                continue;
            }
            if (header->channel == protocol::kDatagramHandshakeChannel) {
                handshake(datagram, received->from);
            } else if (m_control && received->from == m_peer && header->connection == kConnection &&
                       header->channel == protocol::kVideoChannel && datagram.size() == protocol::kDatagramHeaderSize) {
                openVideo();
            } else if (m_control && received->from == m_peer && header->connection == kConnection &&
                       header->channel == protocol::kVideoChannel && protocol::isMediaClosed(datagram)) {
                m_closedAcknowledgements.fetch_add(1);
            } else if (m_control && received->from == m_peer && header->connection == kConnection &&
                       header->channel == protocol::kControlChannel && !lose()) {
                static_cast<void>(m_control->receive(datagram, SteadyClock::now()));
                serve();
            } else if (m_bulk && received->from == m_peer && header->connection == kConnection &&
                       header->channel == protocol::kBulkChannel) {
                static_cast<void>(m_bulk->receive(datagram, SteadyClock::now()));
            }
        }
    }

    void handshake(std::span<const std::uint8_t> datagram, const platform::UdpEndpoint& from)
    {
        if (datagram.size() == protocol::kDatagramHeaderSize) {
            protocol::ServerHello hello;
            hello.nonce = m_nonce;
            static_cast<void>(m_socket.sendTo(from, handshakeDatagram(0, frameOf(hello))));
            return;
        }
        const auto message =
            protocol::decodeToServerFrame(protocol::FrameContext::Handshake, datagram.subspan(protocol::kDatagramHeaderSize));
        const auto* hello = message ? std::get_if<protocol::ClientHello>(&*message) : nullptr;
        if (hello == nullptr) {
            return;
        }
        if (hello->proof != detail::udpHelloProof(token, m_nonce)) {
            protocol::HandshakeError error;
            error.code = protocol::ErrorCode::AuthenticationFailed;
            error.message = "wrong session token";
            static_cast<void>(m_socket.sendTo(from, handshakeDatagram(0, frameOf(error))));
            return;
        }
        static_cast<void>(m_socket.sendTo(from, handshakeDatagram(kConnection)));
        if (!m_control) {
            m_peer = from;
            m_control.emplace(ReliableStream::Kind::Control, kConnection, token, std::size_t{1} << 20);
            protocol::Welcome welcome;
            welcome.model = "Fake Phone";
            welcome.sdkInt = 35;
            m_control->send(frameOf(welcome));
        }
    }

    void openVideo()
    {
        const auto opened = protocol::encodeDatagramHeader({.channel = protocol::kVideoChannel, .connection = kConnection});
        static_cast<void>(m_socket.sendTo(m_peer, opened));
        if (m_videoSent) {
            return;
        }
        m_videoSent = true;
        for (std::uint32_t number = 0; number < videoPackets().size(); ++number) {
            const auto shards = protocol::testing::videoShards(number, videoPackets()[number], 1000, 1, kConnection);
            for (std::size_t i = 0; i < shards.size(); ++i) {
                if (number != 0 || i != 1) {
                    static_cast<void>(m_socket.sendTo(m_peer, shards[i]));
                }
            }
        }
        if (m_closeVideo) {
            const auto closed = protocol::encodeMediaClosed(protocol::kVideoChannel, kConnection);
            static_cast<void>(m_socket.sendTo(m_peer, closed));
            static_cast<void>(m_socket.sendTo(m_peer, closed));
        }
    }

public:
    static const std::vector<std::vector<std::uint8_t>>& videoPackets()
    {
        static const std::vector<std::vector<std::uint8_t>> packets = [] {
            std::vector<std::vector<std::uint8_t>> out{std::vector<std::uint8_t>(3000), std::vector<std::uint8_t>(100)};
            for (auto& packet : out) {
                for (std::size_t i = 0; i < packet.size(); ++i) {
                    packet[i] = static_cast<std::uint8_t>(i * 13 + packet.size());
                }
            }
            return out;
        }();
        return packets;
    }

private:
    void serve()
    {
        while (true) {
            const auto bytes = m_control->readable();
            if (bytes.size() < protocol::kFrameHeaderSize) {
                return;
            }
            const auto header =
                protocol::decodeFrameHeader(protocol::FrameContext::Control, protocol::Direction::ToServer, bytes);
            if (!header || bytes.size() < protocol::kFrameHeaderSize + header->length) {
                return;
            }
            const std::size_t size = protocol::kFrameHeaderSize + header->length;
            const auto message = protocol::decodeToServerFrame(protocol::FrameContext::Control, bytes.first(size));
            m_control->consume(size);
            if (!message) {
                return;
            }
            if (const auto* ping = std::get_if<protocol::Ping>(&*message)) {
                protocol::Pong pong;
                pong.nonce = ping->nonce;
                pong.clientTimeUs = ping->clientTimeUs;
                m_control->send(frameOf(pong));
            } else if (const auto* request = std::get_if<protocol::RequestAppList>(&*message)) {
                if (!m_bulk) {
                    m_bulk.emplace(ReliableStream::Kind::Bulk, kConnection, token, std::size_t{1} << 20);
                }
                protocol::AppList list;
                list.requestId = request->requestId;
                for (int i = 0; i < 200; ++i) {
                    list.apps.push_back({.package = "com.example.app" + std::to_string(i),
                                         .label = "App " + std::to_string(i),
                                         .versionCode = static_cast<std::uint64_t>(i)});
                }
                m_bulk->send(frameOf(list));
            } else if (const auto* shot = std::get_if<protocol::RequestScreenshot>(&*message)) {
                if (!m_bulk) {
                    m_bulk.emplace(ReliableStream::Kind::Bulk, kConnection, token, std::size_t{1} << 20);
                }
                protocol::Screenshot reply;
                reply.requestId = shot->requestId;
                reply.width = 1440;
                reply.height = 3200;
                reply.data.resize(kLargeScreenshotBytes);
                for (std::size_t i = 0; i < reply.data.size(); ++i) {
                    reply.data[i] = static_cast<std::uint8_t>(i * 31);
                }
                m_bulk->send(frameOf(reply));
            } else if (std::holds_alternative<protocol::Goodbye>(*message)) {
                m_goodbye.store(true);
            }
        }
    }

    bool lose() { return m_control && m_loss > 0 && std::uniform_real_distribution<double>(0, 1)(m_random) < m_loss; }

    double m_loss;
    bool m_closeVideo;
    std::atomic<int> m_closedAcknowledgements{0};
    std::mt19937 m_random{7};
    platform::UdpSocket m_socket = *platform::UdpSocket::bind(platform::UdpEndpoint::loopback(0));
    protocol::Nonce m_nonce{0xA5, 0x5A, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    platform::UdpEndpoint m_peer;
    std::optional<ReliableStream> m_control;
    std::optional<ReliableStream> m_bulk;
    std::atomic<bool> m_goodbye{false};
    bool m_videoSent = false;
    std::jthread m_thread;
};

std::vector<std::uint8_t> ping(std::uint32_t nonce)
{
    protocol::Ping message;
    message.nonce = nonce;
    message.clientTimeUs = 1;
    return frameOf(message);
}

protocol::Pong pongFrom(FrameChannel& control)
{
    const auto frame = control.receive();
    REQUIRE(frame.has_value());
    const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Control, *frame);
    REQUIRE(message.has_value());
    const auto* pong = std::get_if<protocol::Pong>(&*message);
    REQUIRE(pong != nullptr);
    return *pong;
}

}

TEST_CASE("the direct transport handshakes, takes the Welcome off the control stream and talks over it")
{
    FakeUdpServer server;
    detail::DirectTransport transport(server.endpoint());
    auto link = transport.establish(token, "test", {});
    REQUIRE(link.has_value());
    CHECK((*link)->kind() == TransportKind::WifiDirect);
    CHECK((*link)->welcome().model == "Fake Phone");
    CHECK((*link)->welcome().sdkInt == 35);

    FrameChannel& control = (*link)->control();
    REQUIRE(control.send(ping(41)));
    CHECK(pongFrom(control).nonce == 41);

    REQUIRE(control.send(frameOf(protocol::Goodbye{})));
    const auto before = SteadyClock::now();
    const auto end = control.receive();
    REQUIRE_FALSE(end.has_value());
    CHECK(end.error().error == ChannelError::Closed);
    CHECK(SteadyClock::now() - before < 500ms);
    CHECK(server.saidGoodbye());
}

TEST_CASE("bulk replies come off the connection's bulk stream, many segments long, without opening a channel")
{
    FakeUdpServer server;
    detail::DirectTransport transport(server.endpoint());
    auto link = transport.establish(token, "test", {});
    REQUIRE(link.has_value());
    auto bulk = (*link)->openBulk({});
    REQUIRE(bulk.has_value());

    protocol::RequestAppList request;
    request.requestId = 9;
    REQUIRE((*link)->control().send(frameOf(request)));
    const auto frame = (*bulk)->receive();
    REQUIRE(frame.has_value());
    CHECK(frame->size() > protocol::kReliableDatagramLimit);
    const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Bulk, *frame);
    REQUIRE(message.has_value());
    const auto* list = std::get_if<protocol::AppList>(&*message);
    REQUIRE(list != nullptr);
    CHECK(list->requestId == 9);
    REQUIRE(list->apps.size() == 200);
    CHECK(list->apps[199].package == "com.example.app199");

    (*bulk)->interrupt();
    const auto after = (*bulk)->receive();
    REQUIRE_FALSE(after.has_value());
    CHECK(after.error().error == ChannelError::Closed);
}

TEST_CASE("a bulk reply larger than 8 MiB, such as a big screenshot, arrives whole over the direct transport (VIS-001)")
{
    FakeUdpServer server;
    detail::DirectTransport transport(server.endpoint());
    auto link = transport.establish(token, "test", {});
    REQUIRE(link.has_value());
    auto bulk = (*link)->openBulk({});
    REQUIRE(bulk.has_value());

    protocol::RequestScreenshot request;
    request.requestId = 0x80000001;
    REQUIRE((*link)->control().send(frameOf(request)));
    const auto frame = (*bulk)->receive();
    REQUIRE(frame.has_value());
    const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Bulk, *frame);
    REQUIRE(message.has_value());
    const auto* shot = std::get_if<protocol::Screenshot>(&*message);
    REQUIRE(shot != nullptr);
    CHECK(shot->requestId == 0x80000001);
    CHECK(shot->width == 1440);
    REQUIRE(shot->data.size() == kLargeScreenshotBytes);
    CHECK(shot->data[12345] == static_cast<std::uint8_t>(12345 * 31));
    CHECK(shot->data[kLargeScreenshotBytes - 1] == static_cast<std::uint8_t>((kLargeScreenshotBytes - 1) * 31));
}

TEST_CASE("a media channel opens with an empty datagram and hands out whole packets, a lost shard rebuilt (SPEC 10.5)")
{
    FakeUdpServer server;
    detail::DirectTransport transport(server.endpoint());
    auto link = transport.establish(token, "test", {});
    REQUIRE(link.has_value());

    auto video = (*link)->openMedia(protocol::MediaChannel::Video, {});
    REQUIRE(video.has_value());
    for (const auto& expected : FakeUdpServer::videoPackets()) {
        const auto packet = (*video)->receive();
        REQUIRE(packet.has_value());
        CHECK(packet->lostBefore == 0);
        CHECK(std::vector<std::uint8_t>(packet->bytes.begin(), packet->bytes.end()) == expected);
    }
    const auto loss = (*video)->takeLossCounts();
    CHECK(loss.lost == 1);
    CHECK(loss.rebuilt == 1);
    CHECK((*video)->takeLossCounts().lost == 0);

    const auto before = SteadyClock::now();
    const auto audio = (*link)->openMedia(protocol::MediaChannel::Audio, {});
    REQUIRE_FALSE(audio.has_value());
    CHECK(audio.error().kind == FailureKind::HandshakeRejected);
    CHECK(audio.error().code == protocol::ErrorCode::ChannelUnavailable);
    CHECK(SteadyClock::now() - before >= 1400ms);
}

TEST_CASE("\"closed\" ends a media channel after its whole packets, and each one is acknowledged (SPEC 10.5)")
{
    FakeUdpServer server(0, true);
    detail::DirectTransport transport(server.endpoint());
    auto link = transport.establish(token, "test", {});
    REQUIRE(link.has_value());
    auto video = (*link)->openMedia(protocol::MediaChannel::Video, {});
    REQUIRE(video.has_value());
    for (const auto& expected : FakeUdpServer::videoPackets()) {
        const auto packet = (*video)->receive();
        REQUIRE(packet.has_value());
        CHECK(std::vector<std::uint8_t>(packet->bytes.begin(), packet->bytes.end()) == expected);
    }
    const auto end = (*video)->receive();
    REQUIRE_FALSE(end.has_value());
    CHECK(end.error().error == ChannelError::Closed);

    const auto deadline = SteadyClock::now() + 2s;
    while (server.closedAcknowledgements() < 2 && SteadyClock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    CHECK(server.closedAcknowledgements() == 2);
}

TEST_CASE("the direct transport reports the server's refusal of a wrong token")
{
    FakeUdpServer server;
    Token wrong = token;
    wrong[0] ^= 1;
    detail::DirectTransport transport(server.endpoint());
    const auto link = transport.establish(wrong, "test", {});
    REQUIRE_FALSE(link.has_value());
    CHECK(link.error().kind == FailureKind::HandshakeRejected);
    CHECK(link.error().code == protocol::ErrorCode::AuthenticationFailed);
}

TEST_CASE("without a server the direct transport gives up after 1.5 s, and a stop cuts it shorter")
{
    auto silent = platform::UdpSocket::bind(platform::UdpEndpoint::loopback(0));
    REQUIRE(silent.has_value());
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(silent->localPort()));

    auto before = SteadyClock::now();
    auto link = transport.establish(token, "test", {});
    auto took = SteadyClock::now() - before;
    REQUIRE_FALSE(link.has_value());
    CHECK(link.error().kind == FailureKind::DirectUnreachable);
    CHECK(took >= 1400ms);
    CHECK(took < 2500ms);

    std::stop_source stop;
    std::jthread stopper([&stop] {
        std::this_thread::sleep_for(100ms);
        stop.request_stop();
    });
    before = SteadyClock::now();
    link = transport.establish(token, "test", stop.get_token());
    took = SteadyClock::now() - before;
    REQUIRE_FALSE(link.has_value());
    CHECK(took < 1000ms);
}

namespace {

class ScriptedTransport final : public Transport
{
public:
    explicit ScriptedTransport(bool succeeds) : m_succeeds(succeeds) {}

    Expected<std::unique_ptr<Link>, SessionFailure> establish(const Token&, const std::string&, std::stop_token) override
    {
        ++m_calls;
        if (!m_succeeds) {
            SessionFailure failure;
            failure.kind = FailureKind::DirectUnreachable;
            failure.detail = "no answer";
            return makeUnexpected(std::move(failure));
        }
        return std::unique_ptr<Link>(std::make_unique<IdleLink>());
    }

    [[nodiscard]] int calls() const { return m_calls; }

private:
    class IdleChannel final : public FrameChannel
    {
    public:
        Expected<std::span<const std::uint8_t>, ChannelFailure> receive() override
        {
            return makeUnexpected(ChannelFailure{ChannelError::Closed});
        }
        bool send(std::span<const std::uint8_t>) override { return true; }
        void setSendTimeout(std::chrono::milliseconds) override {}
        void drain(std::chrono::steady_clock::time_point) override {}
        void interrupt() noexcept override {}
    };

    class IdleLink final : public Link
    {
    public:
        TransportKind kind() const noexcept override { return TransportKind::WifiDirect; }
        const protocol::Welcome& welcome() const override { return m_welcome; }
        FrameChannel& control() override { return m_control; }
        Expected<std::unique_ptr<MediaChannel>, SessionFailure> openMedia(protocol::MediaChannel, std::stop_token) override
        {
            return makeUnexpected(SessionFailure{});
        }

    private:
        protocol::Welcome m_welcome;
        IdleChannel m_control;
    };

    bool m_succeeds;
    int m_calls = 0;
};

}

TEST_CASE("auto, when chosen, goes over the adb tunnel only when WiFi direct fails, and tries direct every time")
{
    auto direct = std::make_unique<ScriptedTransport>(true);
    auto tunnel = std::make_unique<ScriptedTransport>(true);
    const ScriptedTransport& directSeen = *direct;
    const ScriptedTransport& tunnelSeen = *tunnel;
    detail::FallbackTransport working(std::move(direct), std::move(tunnel), "test");
    CHECK(working.establish(token, "test", {}).has_value());
    CHECK(working.establish(token, "test", {}).has_value());
    CHECK(directSeen.calls() == 2);
    CHECK(tunnelSeen.calls() == 0);

    auto failing = std::make_unique<ScriptedTransport>(false);
    auto rescue = std::make_unique<ScriptedTransport>(true);
    const ScriptedTransport& failingSeen = *failing;
    const ScriptedTransport& rescueSeen = *rescue;
    detail::FallbackTransport falling(std::move(failing), std::move(rescue), "test");
    CHECK(falling.establish(token, "test", {}).has_value());
    CHECK(falling.establish(token, "test", {}).has_value());
    CHECK(failingSeen.calls() == 2);
    CHECK(rescueSeen.calls() == 2);

    std::stop_source stopped;
    stopped.request_stop();
    auto cut = std::make_unique<ScriptedTransport>(false);
    auto unused = std::make_unique<ScriptedTransport>(true);
    const ScriptedTransport& unusedSeen = *unused;
    detail::FallbackTransport stopping(std::move(cut), std::move(unused), "test");
    CHECK_FALSE(stopping.establish(token, "test", stopped.get_token()).has_value());
    CHECK(unusedSeen.calls() == 0);
}

TEST_CASE("under 10% loss both ways the control stream keeps every frame and their order (WIF-004)")
{
    FakeUdpServer server(0.10);
    detail::DirectTransport transport(server.endpoint());
    auto link = transport.establish(token, "test", {});
    REQUIRE(link.has_value());
    FrameChannel& control = (*link)->control();
    constexpr std::uint32_t count = 200;
    for (std::uint32_t i = 0; i < count; ++i) {
        REQUIRE(control.send(ping(i)));
    }
    bool ordered = true;
    for (std::uint32_t i = 0; i < count; ++i) {
        ordered = ordered && pongFrom(control).nonce == i;
    }
    CHECK(ordered);
}
