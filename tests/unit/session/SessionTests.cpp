#include "fakes/FakeDeviceServer.h"
#include "fakes/FakeServerHost.h"
#include "fakes/RecordingListener.h"

#include "gs/session/Session.h"

#include <doctest/doctest.h>

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
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
    return options;
}

protocol::ConfigureVideo h264()
{
    protocol::ConfigureVideo configure;
    configure.codec = protocol::VideoCodec::H264;
    configure.maxSize = 1080;
    configure.bitrate = 8'000'000;
    return configure;
}

std::vector<protocol::Bytes> videoStream()
{
    protocol::VideoSessionInfo info;
    info.frameWidth = 1080;
    info.frameHeight = 2400;
    info.displayWidth = 1080;
    info.displayHeight = 2400;
    const std::array<std::uint8_t, 8> config{0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1F};
    const std::array<std::uint8_t, 6> frame{0, 0, 0, 1, 0x65, 0x88};
    std::vector<protocol::Bytes> packets;
    packets.push_back(*protocol::encodeVideoSession(1, info));
    packets.push_back(*protocol::encodeMediaConfig(protocol::MediaChannel::Video, 1, config));
    packets.push_back(*protocol::encodeMediaFrame(protocol::MediaChannel::Video, 1, {true, false}, 1000, 8000, frame));
    packets.push_back(*protocol::encodeMediaFrame(protocol::MediaChannel::Video, 1, {}, 17000, 9000, frame));
    return packets;
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

FakeServerOptions streamingServer()
{
    FakeServerOptions options;
    options.videoPackets = videoStream();
    return options;
}

SessionOptions withVideo(SessionOptions options = fastOptions())
{
    options.video = h264();
    return options;
}

std::vector<protocol::Bytes> audioStream()
{
    protocol::AudioSessionInfo info;
    info.sampleRate = 48000;
    info.channels = 2;
    const std::array<std::uint8_t, 8> head{'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
    const std::array<std::uint8_t, 3> frame{0xFC, 0xFF, 0xFE};
    std::vector<protocol::Bytes> packets;
    packets.push_back(*protocol::encodeAudioSession(1, info));
    packets.push_back(*protocol::encodeMediaConfig(protocol::MediaChannel::Audio, 1, head));
    packets.push_back(*protocol::encodeMediaFrame(protocol::MediaChannel::Audio, 1, {}, 20000, 2500, frame));
    return packets;
}

FakeServerOptions audioServer()
{
    FakeServerOptions options = streamingServer();
    options.audioPackets = audioStream();
    return options;
}

SessionOptions withAudio()
{
    SessionOptions options = withVideo();
    protocol::ConfigureAudio audio;
    audio.enabled = true;
    audio.bitrate = 128'000;
    options.audio = audio;
    return options;
}

struct Rig {
    explicit Rig(FakeServerOptions serverOptions = {}, SessionOptions options = fastOptions())
        : server(std::move(serverOptions))
    {
        auto owned = std::make_unique<FakeServerHost>(server);
        host = owned.get();
        session = std::make_unique<Session>(std::move(owned), std::move(options), listener);
    }

    FakeDeviceServer server;
    RecordingListener listener;
    FakeServerHost* host = nullptr;
    std::unique_ptr<Session> session;
};

SessionFailure failureOf(FailureKind kind, std::string detail)
{
    SessionFailure failure;
    failure.kind = kind;
    failure.detail = std::move(detail);
    return failure;
}

int sessionThreads()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    int count = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) {
            continue;
        }
        const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (thread == nullptr) {
            continue;
        }
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr) {
            if (std::wstring_view(description).starts_with(L"gs-")) {
                ++count;
            }
            LocalFree(description);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return count;
}

bool waitForSessionThreads(int expected)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (sessionThreads() != expected) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

}

TEST_CASE("a session handshakes, streams video and delivers control messages in order")
{
    Rig rig(streamingServer(), withVideo());
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));

    const auto welcomes = rig.listener.welcomes();
    REQUIRE(welcomes.size() == 1);
    CHECK(welcomes[0].serialNo == "FAKE0001");
    CHECK_FALSE(welcomes[0].resumed);
    CHECK(rig.listener.transports() == std::vector{TransportKind::AdbTunnel});
    const LaunchParams params = rig.host->params();
    CHECK(params.socketName.size() == std::string_view("gamescrcpy_").size() + 16);
    CHECK(params.socketName.starts_with("gamescrcpy_"));
    CHECK(std::ranges::any_of(params.token, [](std::uint8_t b) { return b != 0; }));

    REQUIRE(rig.listener.waitForVideo(4));
    const auto video = rig.listener.video();
    CHECK(video[0].kind == protocol::MediaKind::Session);
    CHECK(video[1].kind == protocol::MediaKind::Config);
    CHECK(video[2].kind == protocol::MediaKind::Frame);
    CHECK(video[3].session == 1);

    protocol::Key key;
    key.keycode = 66;
    protocol::Text text;
    text.text = "你好";
    CHECK(rig.session->send(touchDown()));
    CHECK(rig.session->send(key));
    CHECK(rig.session->send(text));
    REQUIRE(rig.server.waitForReceived<protocol::Text>(1));
    std::vector<protocol::ToServerMessage> sent = rig.server.receivedAll();
    std::erase_if(sent, [](const auto& message) {
        return std::holds_alternative<protocol::Ping>(message) || std::holds_alternative<protocol::ReceiverReport>(message);
    });
    REQUIRE(sent.size() == 4);
    CHECK(std::holds_alternative<protocol::ConfigureVideo>(sent[0]));
    CHECK(std::holds_alternative<protocol::TouchFrame>(sent[1]));
    CHECK(std::holds_alternative<protocol::Key>(sent[2]));
    CHECK(std::get<protocol::Text>(sent[3]).text == "你好");

    rig.session->stop();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Stopped));
    REQUIRE(rig.server.waitForReceived<protocol::Goodbye>(1));
    CHECK(rig.server.received<protocol::Goodbye>()[0].reason == protocol::ErrorCode::None);
    CHECK(rig.listener.phases() == std::vector<SessionPhase>{SessionPhase::Deploying, SessionPhase::Launching,
                                                             SessionPhase::Connecting, SessionPhase::Handshaking,
                                                             SessionPhase::Streaming, SessionPhase::Stopping,
                                                             SessionPhase::Stopped});
    CHECK(rig.host->stops() == 1);
    CHECK_FALSE(rig.session->send(key));
}

TEST_CASE("the client reports what the video channel received, every interval and when the session changes")
{
    FakeServerOptions serverOptions = streamingServer();
    std::size_t firstBytes = 0;
    for (const protocol::Bytes& packet : serverOptions.videoPackets) {
        firstBytes += packet.size();
    }
    protocol::VideoSessionInfo rotated;
    rotated.frameWidth = 2400;
    rotated.frameHeight = 1080;
    rotated.displayWidth = 2400;
    rotated.displayHeight = 1080;
    rotated.rotation = 1;
    const std::array<std::uint8_t, 8> config{0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1F};
    const std::array<std::uint8_t, 6> frame{0, 0, 0, 1, 0x65, 0x88};
    serverOptions.videoPackets.push_back(*protocol::encodeVideoSession(2, rotated));
    serverOptions.videoPackets.push_back(*protocol::encodeMediaConfig(protocol::MediaChannel::Video, 2, config));
    serverOptions.videoPackets.push_back(
        *protocol::encodeMediaFrame(protocol::MediaChannel::Video, 2, {true, false}, 40'000, 7'000, frame));
    SessionOptions options = withVideo();
    options.reportInterval = 20ms;
    Rig rig(std::move(serverOptions), std::move(options));
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.server.waitForReceived<protocol::ReceiverReport>(10));
    const auto reports = rig.server.received<protocol::ReceiverReport>();

    const auto second = std::ranges::find_if(reports, [](const auto& report) { return report.videoSession == 2; });
    REQUIRE(second != reports.end());
    std::uint32_t frames = 0;
    std::uint64_t bytes = 0;
    for (auto report = reports.begin(); report != second; ++report) {
        CHECK(report->videoSession == 1);
        frames += report->frames;
        bytes += report->bytes;
    }
    CHECK(frames == 2);
    CHECK(bytes == firstBytes);
    frames = 0;
    for (auto report = second; report != reports.end(); ++report) {
        CHECK(report->videoSession == 2);
        frames += report->frames;
    }
    CHECK(frames == 1);

    const auto both = std::ranges::find_if(reports, [](const auto& report) { return report.frames == 2; });
    if (both != reports.end()) {
        CHECK(both->delayMaxUs - both->delayMinUs <= 17'000);
        CHECK(both->delayMaxUs - both->delayMinUs > 10'000);
        CHECK(both->delayMeanUs == (both->delayMinUs + both->delayMaxUs) / 2);
    }
    CHECK(reports.back().frames == 0);
    CHECK(reports.back().delayMeanUs == 0);
    CHECK(reports.back().roundTripUs > 0);
}

TEST_CASE("heartbeats measure the round trip, the clock offset and the send delay")
{
    FakeServerOptions server;
    server.clockOffsetUs = 5'000'000;
    Rig rig(server);
    rig.session->start();
    REQUIRE(rig.listener.waitForHeartbeats(3));
    const Heartbeat last = rig.listener.heartbeats().back();
    CHECK(last.roundTrip < 500ms);
    CHECK(std::chrono::abs(last.clockOffset - 5s) < 100ms);
    CHECK(last.sendDelay < 500ms);
    CHECK(rig.server.received<protocol::Ping>().size() >= 3);
}

TEST_CASE("an input is followed by a Ping that times its injection, at most one every 100 ms")
{
    FakeServerOptions server;
    server.clockOffsetUs = 5'000'000;
    SessionOptions options = fastOptions();
    options.pingInterval = 60s;
    Rig rig(server, options);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.listener.waitForHeartbeats(1));

    CHECK(rig.session->send(touchDown()));
    CHECK(rig.session->send(touchDown()));
    protocol::Key key;
    key.keycode = 66;
    CHECK(rig.session->send(key));
    REQUIRE(rig.listener.waitForHeartbeats(2));
    std::this_thread::sleep_for(120ms);
    CHECK(rig.session->send(touchDown()));
    REQUIRE(rig.listener.waitForHeartbeats(3));

    std::vector<protocol::ToServerMessage> sent = rig.server.receivedAll();
    std::erase_if(sent, [](const auto& message) { return std::holds_alternative<protocol::ReceiverReport>(message); });
    REQUIRE(sent.size() == 7);
    CHECK(std::holds_alternative<protocol::Ping>(sent[0]));
    CHECK(std::holds_alternative<protocol::TouchFrame>(sent[1]));
    CHECK(std::holds_alternative<protocol::Ping>(sent[2]));
    CHECK(std::holds_alternative<protocol::TouchFrame>(sent[3]));
    CHECK(std::holds_alternative<protocol::Key>(sent[4]));
    CHECK(std::holds_alternative<protocol::TouchFrame>(sent[5]));
    CHECK(std::holds_alternative<protocol::Ping>(sent[6]));

    for (const Heartbeat& heartbeat : rig.listener.heartbeats()) {
        CHECK(heartbeat.sentUs > 0);
        const std::int64_t apart = heartbeat.serverTimeUs - heartbeat.sentUs;
        CHECK(apart >= 5'000'000);
        CHECK(apart <= 5'000'000 + heartbeat.roundTrip.count());
    }
}

TEST_CASE("a protocol version mismatch fails the handshake with a readable reason")
{
    FakeServerOptions server;
    server.protocolVersion = 2;
    Rig rig(server);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Failed));
    const SessionFailure failure = *rig.listener.first(SessionPhase::Failed)->failure;
    CHECK(failure.kind == FailureKind::HandshakeRejected);
    CHECK(failure.code == protocol::ErrorCode::VersionMismatch);
    CHECK(failure.phase == SessionPhase::Handshaking);
    CHECK(failure.detail.find("protocol 2") != std::string::npos);
    CHECK(failure.detail.find("protocol 1") != std::string::npos);
    CHECK(rig.host->stops() == 1);
}

TEST_CASE("a wrong token is refused by the server")
{
    Rig rig;
    rig.host->useServerToken(Token{0x42});
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Failed));
    const SessionFailure failure = *rig.listener.first(SessionPhase::Failed)->failure;
    CHECK(failure.kind == FailureKind::HandshakeRejected);
    CHECK(failure.code == protocol::ErrorCode::AuthenticationFailed);
}

TEST_CASE("a lost control connection resumes with the same token and the current video state")
{
    Rig rig(streamingServer(), withVideo());
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.server.waitForVideoOpens(1));
    protocol::SetVideoBitrate bitrate;
    bitrate.bitrate = 4'000'000;
    protocol::SetVideoPaused pause;
    pause.paused = true;
    CHECK(rig.session->send(bitrate));
    CHECK(rig.session->send(pause));
    REQUIRE(rig.server.waitForReceived<protocol::SetVideoPaused>(1));

    rig.server.dropControl();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming, 2));
    const SessionState reconnecting = *rig.listener.first(SessionPhase::Reconnecting);
    REQUIRE(reconnecting.failure);
    CHECK(reconnecting.failure->kind == FailureKind::ConnectionLost);
    CHECK(reconnecting.failure->phase == SessionPhase::Streaming);
    CHECK(rig.server.resumedFlags() == std::vector<bool>{false, true});
    CHECK(rig.listener.welcomes().back().resumed);
    CHECK(rig.listener.transports() == std::vector{TransportKind::AdbTunnel, TransportKind::AdbTunnel});
    REQUIRE(rig.server.waitForVideoOpens(2));
    REQUIRE(rig.server.waitForReceived<protocol::ConfigureVideo>(2));
    REQUIRE(rig.server.waitForReceived<protocol::SetVideoPaused>(2));
    CHECK(rig.server.received<protocol::ConfigureVideo>().back().bitrate == 4'000'000);
    CHECK(rig.server.received<protocol::SetVideoPaused>().back().paused);

    CHECK(rig.session->send(touchDown()));
    CHECK(rig.server.waitForReceived<protocol::TouchFrame>(1));

    rig.session->stop();
    REQUIRE(rig.listener.waitForEnd());
    CHECK(rig.listener.videoOpenings() == 2);
    CHECK(rig.listener.videoClosings() == 2);
    CHECK(rig.listener.videoThreadCorrect());
}

TEST_CASE("audio streams on a channel of its own, reopened and reconfigured after a loss")
{
    Rig rig(audioServer(), withAudio());
    rig.session->start();
    REQUIRE(rig.listener.waitForAudio(3));
    const auto audio = rig.listener.audio();
    CHECK(audio[0].kind == protocol::MediaKind::Session);
    CHECK(audio[1].kind == protocol::MediaKind::Config);
    CHECK(audio[2].kind == protocol::MediaKind::Frame);
    REQUIRE(rig.listener.waitForVideo(4));
    REQUIRE(rig.server.waitForReceived<protocol::ConfigureAudio>(1));
    CHECK(rig.server.received<protocol::ConfigureAudio>()[0].bitrate == 128'000);

    rig.server.dropControl();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming, 2));
    REQUIRE(rig.server.waitForAudioOpens(2));
    REQUIRE(rig.server.waitForReceived<protocol::ConfigureAudio>(2));
    REQUIRE(rig.listener.waitForAudio(6));

    rig.session->stop();
    REQUIRE(rig.listener.waitForEnd());
    CHECK(rig.listener.audioOpenings() == 2);
    CHECK(rig.listener.audioClosings() == 2);
    CHECK(rig.listener.audioThreadCorrect());
}

TEST_CASE("a device without audio streams video alone")
{
    FakeServerOptions server = audioServer();
    SUBCASE("the Welcome says so")
    {
        server.welcome.audioStatus = protocol::AudioStatus::UnsupportedAndroid;
    }
    SUBCASE("the audio channel is refused")
    {
        server.refuseAudio = true;
    }
    Rig rig(std::move(server), withAudio());
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.listener.waitForVideo(4));

    rig.session->stop();
    REQUIRE(rig.listener.waitForEnd());
    REQUIRE(rig.server.waitForReceived<protocol::Goodbye>(1));
    CHECK(rig.server.audioOpens() == 0);
    CHECK(rig.listener.audioOpenings() == 0);
    CHECK(rig.server.received<protocol::ConfigureAudio>().empty());
}

TEST_CASE("bulk replies arrive on a channel of their own, opened again after a loss")
{
    SessionOptions options = withVideo();
    options.bulk = true;
    Rig rig(streamingServer(), options);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.server.waitForBulkOpens(1));

    protocol::RequestAppList request;
    request.requestId = 3;
    request.includeSystem = true;
    REQUIRE(rig.session->send(request));
    REQUIRE(rig.server.waitForReceived<protocol::RequestAppList>(1));
    CHECK(rig.server.received<protocol::RequestAppList>()[0].requestId == 3);

    protocol::AppList list;
    list.requestId = 3;
    list.apps.push_back({.package = "com.example.game", .label = "Example", .versionCode = 41, .game = true});
    REQUIRE(rig.server.sendBulk(list));
    REQUIRE(rig.listener.waitForMessages(1));
    const auto messages = rig.listener.messages();
    const auto* listed = std::get_if<protocol::AppList>(&messages[0]);
    REQUIRE(listed != nullptr);
    CHECK(*listed == list);

    rig.server.dropControl();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming, 2));
    REQUIRE(rig.server.waitForBulkOpens(2));

    rig.session->stop();
    REQUIRE(rig.listener.waitForEnd());
    CHECK(rig.listener.bulkOpenings() == 2);
    CHECK(rig.listener.bulkClosings() == 2);
}

TEST_CASE("a screenshot request is answered on the bulk channel with the whole picture or with why it failed")
{
    SessionOptions options = withVideo();
    options.bulk = true;
    Rig rig(streamingServer(), options);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.server.waitForBulkOpens(1));

    protocol::RequestScreenshot request;
    request.requestId = 21;
    request.displayId = 0;
    REQUIRE(rig.session->send(request));
    REQUIRE(rig.server.waitForReceived<protocol::RequestScreenshot>(1));
    CHECK(rig.server.received<protocol::RequestScreenshot>()[0] == request);

    protocol::Screenshot screenshot;
    screenshot.requestId = 21;
    screenshot.width = 1080;
    screenshot.height = 2400;
    screenshot.data.resize(std::size_t{9} << 20);
    for (std::size_t i = 0; i < screenshot.data.size(); ++i) {
        screenshot.data[i] = static_cast<std::uint8_t>((i * 31) ^ (i >> 13));
    }
    REQUIRE(rig.server.sendBulk(screenshot));
    REQUIRE(rig.listener.waitForMessages(1));
    {
        const auto messages = rig.listener.messages();
        const auto* arrived = std::get_if<protocol::Screenshot>(&messages[0]);
        REQUIRE(arrived != nullptr);
        CHECK(*arrived == screenshot);
    }

    request.requestId = 22;
    request.displayId = 7;
    REQUIRE(rig.session->send(request));
    REQUIRE(rig.server.waitForReceived<protocol::RequestScreenshot>(2));
    CHECK(rig.server.received<protocol::RequestScreenshot>()[1] == request);
    protocol::RequestFailed failed;
    failed.requestId = 22;
    failed.code = protocol::ErrorCode::NotFound;
    failed.message = "display 7 does not exist";
    REQUIRE(rig.server.sendBulk(failed));
    REQUIRE(rig.listener.waitForMessages(2));
    const auto messages = rig.listener.messages();
    const auto* refused = std::get_if<protocol::RequestFailed>(&messages[1]);
    REQUIRE(refused != nullptr);
    CHECK(*refused == failed);

    rig.session->stop();
    REQUIRE(rig.listener.waitForEnd());
}

TEST_CASE("without asking for it a session opens no bulk channel")
{
    Rig rig(streamingServer(), withVideo());
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    REQUIRE(rig.listener.waitForVideo(4));
    rig.session->stop();
    REQUIRE(rig.listener.waitForEnd());
    CHECK(rig.server.bulkOpens() == 0);
    CHECK(rig.listener.bulkOpenings() == 0);
}

TEST_CASE("resumption waits for the device instead of retrying, and resumes when it is back")
{
    Rig rig;
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    const int connectionsBefore = rig.server.connections();

    rig.server.setMode(FakeDeviceServer::Mode::Refuse);
    rig.server.dropControl();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Reconnecting));
    REQUIRE(rig.host->waitForRestores(2));
    std::this_thread::sleep_for(200ms);
    CHECK(rig.server.connections() == connectionsBefore + 1);
    CHECK(rig.host->restores() == 2);

    rig.server.setMode(FakeDeviceServer::Mode::Normal);
    rig.host->deviceReconnected();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming, 2));
    CHECK(rig.server.resumedFlags() == std::vector<bool>{false, true});
}

TEST_CASE("resumption gives up once the server's linger time has passed")
{
    SessionOptions options = fastOptions();
    options.linger = 300ms;
    Rig rig({}, options);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    rig.server.setMode(FakeDeviceServer::Mode::Refuse);
    rig.server.dropControl();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Failed));
    const SessionFailure failure = *rig.listener.first(SessionPhase::Failed)->failure;
    CHECK(failure.kind == FailureKind::ConnectionLost);
    CHECK(failure.phase == SessionPhase::Reconnecting);
    CHECK(failure.detail.find("not resumed") != std::string::npos);
}

TEST_CASE("missing pongs count as a lost connection, which then resumes")
{
    SessionOptions options = fastOptions();
    options.pingInterval = 30ms;
    options.pongTimeout = 200ms;
    Rig rig({}, options);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    rig.server.setAnswerPings(false);
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Reconnecting));
    const SessionState reconnecting = *rig.listener.first(SessionPhase::Reconnecting);
    CHECK(reconnecting.failure->detail.find("no Pong") != std::string::npos);
    rig.server.setAnswerPings(true);
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming, 2));
    CHECK(rig.server.waitForHandshakes(2));
}

TEST_CASE("the server's goodbye ends the session with its reason")
{
    Rig rig;
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    protocol::ServerGoodbye goodbye;
    goodbye.reason = protocol::ErrorCode::EncoderFailure;
    goodbye.message = "encoder died";
    REQUIRE(rig.server.sendControl(goodbye));
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Failed));
    const SessionFailure failure = *rig.listener.first(SessionPhase::Failed)->failure;
    CHECK(failure.kind == FailureKind::ServerEnded);
    CHECK(failure.code == protocol::ErrorCode::EncoderFailure);
    CHECK(failure.detail.find("encoder died") != std::string::npos);
}

TEST_CASE("a server that exits while reconnecting fails the session at once")
{
    SessionOptions options = fastOptions();
    options.linger = 30s;
    Rig rig({}, options);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    rig.server.setMode(FakeDeviceServer::Mode::Refuse);
    rig.server.dropControl();
    REQUIRE(rig.host->waitForRestores(2));
    const auto ended = std::chrono::steady_clock::now();
    rig.host->endServer(failureOf(FailureKind::ServerEnded, "the server exited with 1"));
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Failed, 1, 2s));
    CHECK(std::chrono::steady_clock::now() - ended < 1s);
    CHECK(rig.listener.first(SessionPhase::Failed)->failure->kind == FailureKind::ServerEnded);
}

TEST_CASE("malformed data from the server ends the session as a protocol error")
{
    Rig rig;
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    const std::array<std::uint8_t, 6> unknown{0x89, 0x99, 0, 0, 0, 0};
    REQUIRE(rig.server.sendControlBytes(unknown));
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Failed));
    CHECK(rig.listener.first(SessionPhase::Failed)->failure->kind == FailureKind::ProtocolError);
    REQUIRE(rig.server.waitForReceived<protocol::Goodbye>(1));
    CHECK(rig.server.received<protocol::Goodbye>()[0].reason == protocol::ErrorCode::MalformedFrame);
}

TEST_CASE("server messages reach the listener in order")
{
    Rig rig;
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
    protocol::Notice notice;
    notice.level = protocol::NoticeLevel::Warning;
    notice.code = protocol::ErrorCode::PermissionDenied;
    notice.message = "injection denied";
    protocol::DisplayChanged changed;
    changed.display.width = 2400;
    changed.display.height = 1080;
    REQUIRE(rig.server.sendControl(notice));
    REQUIRE(rig.server.sendControl(changed));
    REQUIRE(rig.listener.waitForMessages(2));
    const auto messages = rig.listener.messages();
    CHECK(std::get<protocol::Notice>(messages[0]).message == "injection denied");
    CHECK(std::get<protocol::DisplayChanged>(messages[1]).display.width == 2400);
}

TEST_CASE("stopping interrupts a handshake the server never answers")
{
    SessionOptions options = fastOptions();
    options.handshakeTimeout = 30s;
    Rig rig({}, options);
    rig.server.setMode(FakeDeviceServer::Mode::Silent);
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Handshaking));
    REQUIRE(rig.server.waitForConnections(1));
    const auto stopped = std::chrono::steady_clock::now();
    rig.session->stop();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Stopped, 1, 5s));
    CHECK(std::chrono::steady_clock::now() - stopped < 1s);
    CHECK(rig.host->stops() == 1);
}

TEST_CASE("a session stopped before it starts never starts the server")
{
    Rig rig;
    rig.session->stop();
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Stopped));
    CHECK(rig.listener.phases() == std::vector<SessionPhase>{SessionPhase::Stopping, SessionPhase::Stopped});
    CHECK(rig.host->params().socketName.empty());
    CHECK(rig.host->stops() == 1);
}

TEST_CASE("a failed start keeps its kind and the phase it happened in")
{
    Rig rig;
    rig.host->failStart(SessionPhase::Deploying, failureOf(FailureKind::DeviceUnauthorized, "device unauthorized"));
    rig.session->start();
    REQUIRE(rig.listener.waitForPhase(SessionPhase::Failed));
    const SessionFailure failure = *rig.listener.first(SessionPhase::Failed)->failure;
    CHECK(failure.kind == FailureKind::DeviceUnauthorized);
    CHECK(failure.phase == SessionPhase::Deploying);
    CHECK(rig.host->stops() == 1);
    CHECK(rig.server.connections() == 0);
}

TEST_CASE("sessions leave no thread behind")
{
    for (int i = 0; i < 20; ++i) {
        Rig rig(streamingServer(), withVideo());
        rig.session->start();
        REQUIRE(rig.listener.waitForPhase(SessionPhase::Streaming));
        REQUIRE(rig.listener.waitForVideo(1));
        if (i == 0) {
            CHECK(waitForSessionThreads(4));
        }
        CHECK(rig.session->send(touchDown()));
        rig.session->stop();
        REQUIRE(rig.listener.waitForPhase(SessionPhase::Stopped));
    }
    CHECK(sessionThreads() == 0);
}
