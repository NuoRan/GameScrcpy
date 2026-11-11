#include "fakes/FakeDeviceServer.h"
#include "fakes/RecordingListener.h"

#include "gs/platform/TcpStream.h"
#include "gs/session/Session.h"
#include "gs/session/StreamHost.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace gs::session::test;
using namespace std::chrono_literals;

namespace {

SessionOptions fastOptions()
{
    SessionOptions options;
    options.clientVersion = "test";
    options.linger = 5s;
    options.handshakeTimeout = 2s;
    options.pingInterval = 50ms;
    options.pongTimeout = 2s;
    options.stopGrace = 1s;
    protocol::ConfigureVideo configure;
    configure.codec = protocol::VideoCodec::H264;
    configure.maxSize = 1080;
    configure.bitrate = 8'000'000;
    options.video = configure;
    return options;
}

FakeServerOptions streamingServer()
{
    protocol::VideoSessionInfo info;
    info.frameWidth = 1080;
    info.frameHeight = 2400;
    info.displayWidth = 1080;
    info.displayHeight = 2400;
    const std::array<std::uint8_t, 8> config{0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1F};
    const std::array<std::uint8_t, 6> frame{0, 0, 0, 1, 0x65, 0x88};
    FakeServerOptions options;
    options.videoPackets.push_back(*protocol::encodeVideoSession(1, info));
    options.videoPackets.push_back(*protocol::encodeMediaConfig(protocol::MediaChannel::Video, 1, config));
    options.videoPackets.push_back(
        *protocol::encodeMediaFrame(protocol::MediaChannel::Video, 1, {true, false}, 1000, 8000, frame));
    options.videoPackets.push_back(*protocol::encodeMediaFrame(protocol::MediaChannel::Video, 1, {}, 17000, 9000, frame));
    return options;
}

protocol::TouchFrame touchDown()
{
    protocol::TouchFrame touch;
    touch.videoSession = 1;
    protocol::TouchPoint point;
    point.action = protocol::TouchAction::Down;
    point.x = 100;
    point.y = 200;
    point.pressure = 0xFFFF;
    touch.points.push_back(point);
    return touch;
}

std::span<const std::uint8_t> bytesOf(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

struct Pump {
    explicit Pump(platform::TcpStream opened) : tcp(std::move(opened)) {}

    ~Pump()
    {
        tcp.interrupt();
        if (thread.joinable()) {
            thread.join();
        }
    }

    Pump(const Pump&) = delete;
    Pump& operator=(const Pump&) = delete;

    void start(const std::shared_ptr<TransportStream>& carried)
    {
        stream = carried;
        thread = std::thread([this] {
            std::array<std::uint8_t, 4096> buffer{};
            while (true) {
                const std::size_t received = tcp.readSome(buffer);
                const std::shared_ptr<TransportStream> alive = stream.lock();
                if (received == 0) {
                    if (alive != nullptr) {
                        alive->closed(why());
                    }
                    return;
                }
                if (alive != nullptr) {
                    alive->deliver(std::span<const std::uint8_t>(buffer).first(received));
                }
            }
        });
    }

    void drop(std::string reason)
    {
        {
            const std::lock_guard lock(mutex);
            dropped = std::move(reason);
        }
        tcp.interrupt();
    }

    std::string why()
    {
        const std::lock_guard lock(mutex);
        return dropped.empty() ? std::string("the fake server closed the connection") : dropped;
    }

    platform::TcpStream tcp;
    std::weak_ptr<TransportStream> stream;
    std::mutex mutex;
    std::string dropped;
    std::thread thread;
};

class BridgedPhone final : public PhoneStreams
{
public:
    explicit BridgedPhone(FakeDeviceServer& server) : m_server(server) {}

    Expected<std::unique_ptr<ByteStream>, SessionFailure> open(StreamChannel channel, const LaunchParams& params,
                                                              std::chrono::milliseconds timeout,
                                                              std::stop_token stop) override
    {
        std::optional<std::string> refusal;
        {
            const std::lock_guard lock(m_mutex);
            m_opened.push_back(channel);
            m_params = params;
            refusal = m_refusal;
            m_seen = m_generation;
        }
        m_changed.notify_all();
        auto made = std::make_shared<std::shared_ptr<Pump>>();
        auto stream = std::make_shared<TransportStream>(
            [made](std::span<const std::uint8_t> bytes) {
                return *made != nullptr && (*made)->tcp.writeAll(bytes) ? StreamSend::Taken : StreamSend::Failed;
            },
            [made] {
                if (*made != nullptr) {
                    (*made)->tcp.interrupt();
                }
            });
        if (refusal) {
            stream->closed(*refusal);
            return awaitStream(std::move(stream), "the bridge", timeout, stop);
        }
        m_server.setToken(params.token);
        std::optional<platform::TcpStream> tcp = platform::TcpStream::connectLoopback(m_server.port());
        if (!tcp) {
            stream->closed("the fake server does not listen");
            return awaitStream(std::move(stream), "the bridge", timeout, stop);
        }
        auto pump = std::make_shared<Pump>(std::move(*tcp));
        *made = pump;
        pump->start(stream);
        {
            const std::lock_guard lock(m_mutex);
            m_pumps.push_back(pump);
        }
        stream->opened();
        return awaitStream(std::move(stream), "the bridge", timeout, stop);
    }

    Expected<void, SessionFailure> awaitChange(std::chrono::steady_clock::time_point deadline,
                                               std::stop_token stop) override
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_until(lock, stop, deadline, [this] { return m_generation != m_seen; })) {
            SessionFailure failure;
            failure.kind = FailureKind::ConnectionLost;
            failure.detail = "the phone did not come back through the bridge";
            return makeUnexpected(std::move(failure));
        }
        return {};
    }

    void refuse(std::optional<std::string> reason)
    {
        const std::lock_guard lock(m_mutex);
        m_refusal = std::move(reason);
    }

    void change()
    {
        {
            const std::lock_guard lock(m_mutex);
            ++m_generation;
        }
        m_changed.notify_all();
    }

    void dropAll(const std::string& reason)
    {
        std::vector<std::shared_ptr<Pump>> pumps;
        {
            const std::lock_guard lock(m_mutex);
            pumps = m_pumps;
        }
        for (const std::shared_ptr<Pump>& pump : pumps) {
            if (const std::shared_ptr<TransportStream> carried = pump->stream.lock()) {
                carried->closed(reason);
            }
        }
        for (const std::shared_ptr<Pump>& pump : pumps) {
            pump->drop(reason);
        }
    }

    bool waitForOpens(std::size_t count, std::chrono::milliseconds timeout = 5s)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_opened.size() >= count; });
    }

    [[nodiscard]] std::vector<StreamChannel> opened() const
    {
        const std::lock_guard lock(m_mutex);
        return m_opened;
    }

    [[nodiscard]] LaunchParams params() const
    {
        const std::lock_guard lock(m_mutex);
        return m_params;
    }

private:
    FakeDeviceServer& m_server;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::vector<StreamChannel> m_opened;
    LaunchParams m_params;
    std::optional<std::string> m_refusal;
    std::uint64_t m_generation = 0;
    std::uint64_t m_seen = 0;
    std::vector<std::shared_ptr<Pump>> m_pumps;
};

struct Rig {
    explicit Rig(FakeServerOptions serverOptions = streamingServer())
        : server(std::move(serverOptions)), phone(std::make_shared<BridgedPhone>(server))
    {
        session = std::make_unique<Session>(std::make_unique<StreamServerHost>(phone), fastOptions(), listener);
    }

    ~Rig() { session.reset(); }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    FakeDeviceServer server;
    std::shared_ptr<BridgedPhone> phone;
    RecordingListener listener;
    std::unique_ptr<Session> session;
};

struct Sent {
    std::mutex mutex;
    std::vector<std::size_t> chunks;
    StreamSend answer = StreamSend::Taken;
    int closes = 0;

    TransportStream::Sender sender()
    {
        return [this](std::span<const std::uint8_t> bytes) {
            const std::lock_guard lock(mutex);
            chunks.push_back(bytes.size());
            return answer;
        };
    }

    TransportStream::Closer closer()
    {
        return [this] {
            const std::lock_guard lock(mutex);
            ++closes;
        };
    }

    void answerWith(StreamSend next)
    {
        const std::lock_guard lock(mutex);
        answer = next;
    }

    std::size_t tries()
    {
        const std::lock_guard lock(mutex);
        return chunks.size();
    }

    int closed()
    {
        const std::lock_guard lock(mutex);
        return closes;
    }
};

}

TEST_CASE("ADR-0112: a transport stream hands out what came in, holds back while the transport is busy, and tells "
          "the transport once when it is done")
{
    CHECK(streamChannelName(StreamChannel::Control) == "control");
    CHECK(streamChannelName(StreamChannel::Bulk) == "bulk");

    Sent sent;
    auto stream = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    CHECK(stream->awaitOpen(std::chrono::steady_clock::now() + 10ms, {}) == TransportStream::Opening::TimedOut);
    stream->opened();
    CHECK(stream->awaitOpen(std::chrono::steady_clock::now() + 10ms, {}) == TransportStream::Opening::Opened);
    std::unique_ptr<ByteStream> end = sessionEndOf(stream);

    stream->deliver(bytesOf("abc"));
    std::array<std::uint8_t, 2> two{};
    REQUIRE(end->readExact(two));
    CHECK(two == std::array<std::uint8_t, 2>{'a', 'b'});
    std::array<std::uint8_t, 8> some{};
    CHECK(end->readSome(some) == 1);
    CHECK(some[0] == 'c');
    end->setReadTimeout(20ms);
    CHECK(end->readSome(some) == 0);
    CHECK(end->readTimedOut());

    const std::vector<std::uint8_t> big(TransportStream::kChunkBytes * 2 + 10, 7);
    REQUIRE(end->writeAll(big));
    CHECK(sent.chunks == std::vector<std::size_t>{TransportStream::kChunkBytes, TransportStream::kChunkBytes, 10});

    sent.answerWith(StreamSend::Busy);
    bool wrote = false;
    std::thread writer([&] { wrote = end->writeAll(bytesOf("xyz")); });
    while (sent.tries() < 4) {
        std::this_thread::sleep_for(1ms);
    }
    sent.answerWith(StreamSend::Taken);
    stream->writable();
    writer.join();
    CHECK(wrote);
    CHECK(sent.tries() == 5);

    sent.answerWith(StreamSend::Busy);
    end->setWriteTimeout(30ms);
    CHECK_FALSE(end->writeAll(bytesOf("late")));
    sent.answerWith(StreamSend::Taken);

    stream->deliver(bytesOf("xy"));
    stream->closed("the relay went away");
    REQUIRE(end->readExact(two));
    CHECK(two == std::array<std::uint8_t, 2>{'x', 'y'});
    CHECK(end->readSome(some) == 0);
    CHECK_FALSE(end->readTimedOut());
    CHECK(end->closedReason() == "the relay went away");
    CHECK_FALSE(end->writeAll(bytesOf("after")));
    end.reset();
    CHECK(sent.closed() == 0);

    auto other = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    other->opened();
    std::unique_ptr<ByteStream> otherEnd = sessionEndOf(other);
    sent.answerWith(StreamSend::Failed);
    CHECK_FALSE(otherEnd->writeAll(bytesOf("lost")));
    CHECK_FALSE(otherEnd->closedReason().empty());
    otherEnd.reset();
    CHECK(sent.closed() == 1);
    other->shut();
    CHECK(sent.closed() == 1);

    auto waiting = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    waiting->opened();
    std::unique_ptr<ByteStream> waitingEnd = sessionEndOf(waiting);
    bool read = true;
    std::thread reader([&] { read = waitingEnd->readExact(two); });
    waitingEnd->interrupt();
    reader.join();
    CHECK_FALSE(read);

    auto flood = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    flood->opened();
    std::unique_ptr<ByteStream> floodEnd = sessionEndOf(flood);
    const std::vector<std::uint8_t> megabyte(1024 * 1024, 1);
    for (std::size_t round = 0; round <= TransportStream::kMostHeldBytes / megabyte.size(); ++round) {
        flood->deliver(megabyte);
    }
    CHECK(floodEnd->readSome(some) == 0);
    CHECK(floodEnd->closedReason().find("more came from the phone") != std::string::npos);
}

TEST_CASE("ADR-0112: waiting for a stream to open gives the session end, or the transport's reason, a time-out or a "
          "stop")
{
    Sent sent;
    auto refused = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    refused->closed("nobody answers");
    const auto refusal = awaitStream(refused, "Relay", 1s, {});
    REQUIRE_FALSE(refusal.has_value());
    CHECK(refusal.error().kind == FailureKind::ConnectFailed);
    CHECK(refusal.error().detail == "Relay could not reach the phone: nobody answers");
    CHECK(sent.closed() == 0);

    auto silent = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    const auto late = awaitStream(silent, "Relay", 20ms, {});
    REQUIRE_FALSE(late.has_value());
    CHECK(late.error().kind == FailureKind::Timeout);
    CHECK(sent.closed() == 1);

    std::stop_source stopping;
    stopping.request_stop();
    auto stopped = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    CHECK_FALSE(awaitStream(stopped, "Relay", 1s, stopping.get_token()).has_value());
    CHECK(sent.closed() == 2);

    auto ready = std::make_shared<TransportStream>(sent.sender(), sent.closer());
    ready->opened();
    auto opened = awaitStream(ready, "Relay", 1s, {});
    REQUIRE(opened.has_value());
    REQUIRE(*opened != nullptr);
    opened->reset();
    CHECK(sent.closed() == 3);
}

TEST_CASE("ADR-0112: a session runs the real protocol over streams that an extension's transport carries and "
          "reaches the picture")
{
    Rig rig;
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    CHECK(rig.listener.transports() == std::vector{TransportKind::Extension});
    const LaunchParams params = rig.phone->params();
    CHECK(params.socketName.starts_with("gamescrcpy_"));

    REQUIRE(rig.listener.waitForVideo(4));
    const auto video = rig.listener.video();
    CHECK(video[0].kind == protocol::MediaKind::Session);
    CHECK(video[1].kind == protocol::MediaKind::Config);
    CHECK(video[2].kind == protocol::MediaKind::Frame);
    CHECK(rig.phone->opened() == std::vector{StreamChannel::Control, StreamChannel::Video});

    CHECK(rig.session->send(touchDown()));
    REQUIRE(rig.server.waitForReceived<protocol::TouchFrame>(1));
    CHECK(rig.server.handshakes() == 1);
    CHECK(rig.server.videoOpens() == 1);

    rig.session->stop();
    REQUIRE(rig.listener.waitForEnd());
    CHECK(rig.listener.phases().back() == SessionPhase::Stopped);
    CHECK(rig.server.waitForReceived<protocol::Goodbye>(1));
}

TEST_CASE("ADR-0112: when the transport closes its streams the session reconnects as after a dropped tunnel, says "
          "why, and comes back through the transport")
{
    Rig rig;
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.listener.waitForVideo(1));

    rig.phone->dropAll("the relay went away");
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Reconnecting));
    const std::optional<SessionState> reconnecting = rig.listener.first(SessionPhase::Reconnecting);
    REQUIRE(reconnecting.has_value());
    REQUIRE(reconnecting->failure.has_value());
    INFO(reconnecting->failure->detail);
    CHECK(reconnecting->failure->kind == FailureKind::ConnectionLost);
    CHECK(reconnecting->failure->detail.find("the relay went away") != std::string::npos);

    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming, 2));
    const auto welcomes = rig.listener.welcomes();
    REQUIRE(welcomes.size() == 2);
    CHECK(welcomes[1].resumed);
    CHECK(rig.listener.transports() == std::vector{TransportKind::Extension, TransportKind::Extension});
}

TEST_CASE("ADR-0112: after a lost connection the session tries again when the transport says its phones changed, "
          "and fails with the transport's reason when it cannot reach the phone")
{
    Rig rig;
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.phone->waitForOpens(2));

    rig.phone->refuse("the phone is asleep");
    rig.phone->dropAll("the relay went away");
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Reconnecting));
    REQUIRE(rig.phone->waitForOpens(3));
    std::this_thread::sleep_for(200ms);
    CHECK(rig.phone->opened().size() == 3);

    rig.phone->refuse(std::nullopt);
    rig.phone->change();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming, 2));
    CHECK(rig.phone->opened().size() == 5);

    Rig refused;
    refused.phone->refuse("the phone is asleep");
    refused.session->start();
    REQUIRE(refused.listener.waitForEnd());
    const std::optional<SessionFailure> failure = refused.listener.failure();
    REQUIRE(failure.has_value());
    CHECK(failure->kind == FailureKind::ConnectFailed);
    CHECK(failure->detail.find("the phone is asleep") != std::string::npos);
    CHECK(refused.server.connections() == 0);
}
