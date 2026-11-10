// Runs gs_session end to end: the real adb server, a device or emulator, and the real server build.
//
//   GS_ADB_TEST_SERIAL        device to test (required; the program reports "skipped" without it)
//   GS_ADB_TEST_OTHER_SERIAL  a second device, streamed at the same time (SES-002)
//   GS_ADB_TEST_RESTART=1     also kill the adb server mid-session and resume once it is back (SES-006)
//   GS_ADB_EXECUTABLE         adb.exe for that restart (default %ANDROID_HOME%\platform-tools)
//   GS_SERVER_APK             the server build (default: the Gradle release output)
//   GS_SESSION_TEST_CYCLES    sessions opened and closed in a row (default 5; SES-004 asks for 100)
//   GS_SESSION_TEST_LAUNCHES  launches in a row that must not race (default 5; SES-007 asks for 1000)
//   GS_SESSION_TEST_LOG=1     print every session and server log line, not only warnings
//
// The WiFi direct cases need a device on wireless adb (GS_ADB_TEST_SERIAL=ip:port) and skip
// otherwise: an emulator's UDP does not reach its Android from the host.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "fakes/LossyRelay.h"
#include "fakes/RecordingListener.h"

#include "gs/adb/AdbClient.h"
#include "gs/adb/ServerLauncher.h"
#include "gs/base/Log.h"
#include "gs/platform/Crypto.h"
#include "gs/platform/TcpStream.h"
#include "gs/platform/UdpSocket.h"
#include "gs/protocol/Codec.h"
#include "gs/session/AdbServerHost.h"
#include "gs/session/Session.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <miniz.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace gs::session::test;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

std::string g_serial;
std::string g_otherSerial;
bool g_allowRestart = false;
std::filesystem::path g_adbExecutable;
std::shared_ptr<const ServerPackage> g_package;
int g_cycles = 5;
int g_launches = 5;
const adb::AdbClient g_adb;

std::string environment(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, name);
    std::string text = value != nullptr ? value : "";
    std::free(value);
    return text;
}

class StderrSink final : public log::Sink
{
public:
    explicit StderrSink(bool everything) : m_everything(everything) {}

    void write(const log::Record& record) noexcept override
    {
        if (m_everything || record.level >= log::Level::Warning) {
            std::fprintf(stderr, "  [%.*s] %.*s\n", static_cast<int>(record.category.size()), record.category.data(),
                         static_cast<int>(record.message.size()), record.message.data());
        }
    }

private:
    bool m_everything;
};

std::string shell(const std::string& serial, std::string_view command)
{
    const auto result = g_adb.shell(serial, command);
    return result ? result->out : std::string{};
}

bool serverRunning(const std::string& serial)
{
    return shell(serial, "pgrep -f 'gamescrcpy.server.Ma[i]n'").find_first_not_of(" \r\n") != std::string::npos;
}

bool serverGone(const std::string& serial)
{
    const auto deadline = Clock::now() + 5s;
    while (serverRunning(serial)) {
        if (Clock::now() >= deadline) {
            MESSAGE("still running on " << serial << ":\n"
                                        << shell(serial, "ps -A -o PID,STIME,TIME,ARGS | grep '[g]amescrcpy.server'"));
            return false;
        }
        std::this_thread::sleep_for(200ms);
    }
    return true;
}

bool udpPortOpen(const std::string& serial, std::uint16_t port)
{
    const std::string suffix = std::format(":{:04X}", port);
    std::istringstream table(shell(serial, "cat /proc/net/udp /proc/net/udp6"));
    for (std::string line; std::getline(table, line);) {
        std::istringstream fields(line);
        std::string slot;
        std::string local;
        if (fields >> slot >> local && local.ends_with(suffix)) {
            return true;
        }
    }
    return false;
}

std::optional<double> closedAfter(const std::string& serial, std::uint16_t port, Clock::time_point since,
                                  Clock::time_point deadline)
{
    while (udpPortOpen(serial, port)) {
        if (Clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(100ms);
    }
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

bool forwarded(const std::string& serial)
{
    const auto forwards = g_adb.forwards();
    return forwards && std::ranges::any_of(*forwards, [&](const adb::ForwardEntry& entry) {
               return entry.serial == serial && entry.device.starts_with("localabstract:gamescrcpy_");
           });
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

DWORD handleCount()
{
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

double privateMegabytes()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
}

SessionOptions options(bool video)
{
    SessionOptions options;
    options.clientVersion = "device-test";
    if (video) {
        protocol::ConfigureVideo configure;
        configure.codec = protocol::VideoCodec::H264;
        configure.maxSize = 720;
        configure.bitrate = 2'000'000;
        options.video = configure;
    }
    return options;
}

std::unique_ptr<Session> makeSession(const std::string& serial, RecordingListener& listener, SessionOptions sessionOptions)
{
    return std::make_unique<Session>(std::make_unique<AdbServerHost>(g_adb, serial, g_package), std::move(sessionOptions),
                                     listener);
}

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool killAdbServer()
{
    std::optional<platform::TcpStream> stream = platform::TcpStream::connectLoopback(adb::AdbClient::kDefaultPort);
    if (!stream || !stream->writeAll(std::string_view("0009host:kill"))) {
        return false;
    }
    std::array<std::uint8_t, 64> reply{};
    while (stream->readSome(reply) != 0) {
    }
    return true;
}

constexpr std::uint16_t kDirectPort = 27183;

std::optional<platform::UdpEndpoint> deviceAddress(std::uint16_t port)
{
    const auto colon = g_serial.rfind(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    return platform::UdpEndpoint::parse(std::string_view(g_serial).substr(0, colon), port);
}

class QuietHostEvents final : public ServerHost::Events
{
public:
    void hostPhase(SessionPhase) override {}
    void serverEnded(SessionFailure) override {}
};

template <typename M>
std::vector<std::uint8_t> frameOf(const M& message)
{
    return *protocol::encodeFrame(message);
}

std::int64_t steadyMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

bool bulkOpened(const RecordingListener& listener, Clock::time_point deadline)
{
    while (listener.bulkOpenings() == 0) {
        if (Clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

std::optional<protocol::ToClientMessage> bulkReply(RecordingListener& listener, std::uint32_t requestId,
                                                   Clock::time_point deadline)
{
    std::size_t seen = 0;
    while (true) {
        const auto messages = listener.messages();
        for (; seen < messages.size(); ++seen) {
            const auto* screenshot = std::get_if<protocol::Screenshot>(&messages[seen]);
            const auto* failed = std::get_if<protocol::RequestFailed>(&messages[seen]);
            if ((screenshot != nullptr && screenshot->requestId == requestId) ||
                (failed != nullptr && failed->requestId == requestId)) {
                return messages[seen];
            }
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left <= 0ms || !listener.waitForMessages(seen + 1, left)) {
            return std::nullopt;
        }
    }
}

bool heartbeatAfter(RecordingListener& listener, std::int64_t sentUs, Clock::time_point deadline)
{
    while (true) {
        const auto heartbeats = listener.heartbeats();
        if (std::ranges::any_of(heartbeats, [&](const Heartbeat& heartbeat) { return heartbeat.sentUs > sentUs; })) {
            return true;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left <= 0ms || !listener.waitForHeartbeats(heartbeats.size() + 1, left)) {
            return false;
        }
    }
}

protocol::Display mainDisplay(const protocol::Welcome& welcome, const std::vector<protocol::ToClientMessage>& messages)
{
    protocol::Display primary;
    for (const protocol::Display& display : welcome.displays) {
        if (display.displayId == 0) {
            primary = display;
        }
    }
    for (const protocol::ToClientMessage& message : messages) {
        const auto* changed = std::get_if<protocol::DisplayChanged>(&message);
        if (changed != nullptr && changed->display.displayId == 0) {
            primary = changed->display;
        }
    }
    return primary;
}

struct PngSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

std::uint32_t bigEndian32(std::span<const std::uint8_t> bytes)
{
    return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) | (std::uint32_t{bytes[2]} << 8) |
           std::uint32_t{bytes[3]};
}

std::optional<PngSize> decodePng(std::span<const std::uint8_t> png)
{
    constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (png.size() < kSignature.size() || !std::ranges::equal(png.first(kSignature.size()), kSignature)) {
        return std::nullopt;
    }
    PngSize size;
    std::size_t channels = 0;
    std::vector<std::uint8_t> compressed;
    std::size_t offset = kSignature.size();
    bool ended = false;
    while (!ended) {
        if (png.size() - offset < 12) {
            return std::nullopt;
        }
        const std::uint32_t length = bigEndian32(png.subspan(offset, 4));
        if (png.size() - offset - 12 < length) {
            return std::nullopt;
        }
        const auto chunk = png.subspan(offset + 4, std::size_t{4} + length);
        if (mz_crc32(MZ_CRC32_INIT, chunk.data(), chunk.size()) != bigEndian32(png.subspan(offset + 8 + length, 4))) {
            return std::nullopt;
        }
        const std::string_view type(reinterpret_cast<const char*>(chunk.data()), 4);
        const auto data = chunk.subspan(4);
        if (type == "IHDR") {
            if (data.size() != 13 || data[8] != 8 || data[10] != 0 || data[11] != 0 || data[12] != 0) {
                return std::nullopt;
            }
            size.width = bigEndian32(data);
            size.height = bigEndian32(data.subspan(4));
            channels = data[9] == 2 ? 3U : data[9] == 6 ? 4U : 0U;
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), data.begin(), data.end());
        } else if (type == "IEND") {
            ended = true;
        }
        offset += std::size_t{12} + length;
    }
    if (channels == 0 || size.width == 0 || size.height == 0) {
        return std::nullopt;
    }
    const std::size_t stride = 1 + std::size_t{size.width} * channels;
    std::vector<std::uint8_t> rows(stride * size.height);
    auto inflated = static_cast<mz_ulong>(rows.size());
    if (mz_uncompress(rows.data(), &inflated, compressed.data(), static_cast<mz_ulong>(compressed.size())) != MZ_OK ||
        inflated != rows.size()) {
        return std::nullopt;
    }
    for (std::size_t row = 0; row < size.height; ++row) {
        if (rows[row * stride] > 4) {
            return std::nullopt;
        }
    }
    return size;
}

double nearestRank(const std::vector<double>& sorted, double quantile)
{
    const auto rank = static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

}

TEST_CASE("the server build stays under 200 KB (SRV-009)")
{
    MESSAGE(std::format("server build: {} bytes as {}", g_package->bytes().size(), g_package->remotePath()));
    CHECK(g_package->bytes().size() < 200 * 1024);
}

TEST_CASE("a session deploys, launches, streams and ends cleanly on the real server")
{
    shell(g_serial, "rm -f /data/local/tmp/gs-server-*.jar; echo old > /data/local/tmp/gs-server-0000000000000000.jar");

    RecordingListener listener;
    const auto start = Clock::now();
    auto session = makeSession(g_serial, listener, options(true));
    session->start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 1, 30s));
    MESSAGE(std::format("streaming {:.0f} ms after start, including the push", millisecondsSince(start)));

    const auto deployed = g_adb.stat(g_serial, g_package->remotePath());
    REQUIRE(deployed);
    REQUIRE(*deployed);
    CHECK((*deployed)->size == g_package->bytes().size());
    const auto old = g_adb.stat(g_serial, "/data/local/tmp/gs-server-0000000000000000.jar");
    REQUIRE(old);
    CHECK_FALSE(*old);

    const auto welcome = listener.welcomes().at(0);
    CHECK(welcome.sdkInt >= AdbServerHost::kMinimumSdk);
    CHECK_FALSE(welcome.serialNo.empty());
    CHECK_FALSE(welcome.displays.empty());

    REQUIRE(listener.waitForVideo(3, 15s));
    const auto video = listener.video();
    CHECK(video[0].kind == protocol::MediaKind::Session);
    CHECK(video[1].kind == protocol::MediaKind::Config);
    CHECK(video[2].kind == protocol::MediaKind::Frame);

    protocol::TouchFrame touch;
    touch.videoSession = video[0].session;
    protocol::TouchPoint point;
    point.x = 8;
    point.y = 8;
    point.pressure = 0xFFFF;
    touch.points.push_back(point);
    CHECK(session->send(touch));
    touch.points[0].action = protocol::TouchAction::Up;
    CHECK(session->send(touch));
    CHECK(forwarded(g_serial));

    session->stop();
    REQUIRE(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));
    CHECK(listener.phases() == std::vector<SessionPhase>{SessionPhase::Deploying, SessionPhase::Launching,
                                                         SessionPhase::Connecting, SessionPhase::Handshaking,
                                                         SessionPhase::Streaming, SessionPhase::Stopping,
                                                         SessionPhase::Stopped});
    for (const auto& message : listener.messages()) {
        if (const auto* notice = std::get_if<protocol::Notice>(&message)) {
            CHECK_MESSAGE(notice->level != protocol::NoticeLevel::Error, notice->message);
        }
    }
    CHECK_FALSE(forwarded(g_serial));
    CHECK(serverGone(g_serial));
}

TEST_CASE("the next session finds its server deployed and pushes nothing")
{
    const auto before = g_adb.stat(g_serial, g_package->remotePath());
    REQUIRE(before);
    REQUIRE(*before);
    std::this_thread::sleep_for(1100ms);

    RecordingListener listener;
    auto session = makeSession(g_serial, listener, options(false));
    session->start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 1, 20s));
    session->stop();
    REQUIRE(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));

    const auto after = g_adb.stat(g_serial, g_package->remotePath());
    REQUIRE(after);
    REQUIRE(*after);
    CHECK((*after)->mtime == (*before)->mtime);
}

TEST_CASE("a lossless screenshot arrives on the bulk channel while the control channel keeps its pace (VIS-001)")
{
    RecordingListener listener;
    SessionOptions sessionOptions = options(true);
    sessionOptions.bulk = true;
    sessionOptions.pingInterval = 20ms;
    auto session = makeSession(g_serial, listener, sessionOptions);
    session->start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 1, 30s));
    REQUIRE(bulkOpened(listener, Clock::now() + 10s));
    const protocol::Welcome welcome = listener.welcomes().at(0);
    REQUIRE((welcome.capabilities & protocol::Welcome::kScreenshot) != 0);
    REQUIRE(listener.waitForVideo(3, 15s));
    REQUIRE(listener.waitForHeartbeats(listener.heartbeats().size() + 25, 10s));

    protocol::RequestScreenshot request;
    request.requestId = 1;
    request.displayId = 0;
    const auto requested = Clock::now();
    const std::int64_t requestedUs = steadyMicros();
    REQUIRE(session->send(request));
    const auto reply = bulkReply(listener, request.requestId, requested + 15s);
    const std::int64_t answeredUs = steadyMicros();
    const double captureMs = millisecondsSince(requested);
    REQUIRE(reply.has_value());
    if (const auto* failed = std::get_if<protocol::RequestFailed>(&*reply)) {
        FAIL("the screenshot failed with " << protocol::enumName(failed->code) << ": " << failed->message);
    }
    const auto* screenshot = std::get_if<protocol::Screenshot>(&*reply);
    REQUIRE(screenshot != nullptr);
    const protocol::Display primary = mainDisplay(welcome, listener.messages());
    CHECK(screenshot->width == primary.width);
    CHECK(screenshot->height == primary.height);
    CHECK(screenshot->format == protocol::ScreenshotFormat::Png);
    const auto decoded = decodePng(screenshot->data);
    REQUIRE(decoded.has_value());
    CHECK(decoded->width == screenshot->width);
    CHECK(decoded->height == screenshot->height);

    REQUIRE(heartbeatAfter(listener, answeredUs, Clock::now() + 5s));
    std::vector<double> during;
    std::vector<double> before;
    for (const Heartbeat& heartbeat : listener.heartbeats()) {
        const double milliseconds = static_cast<double>(heartbeat.roundTrip.count()) / 1000.0;
        if (heartbeat.sentUs >= requestedUs && heartbeat.sentUs <= answeredUs) {
            during.push_back(milliseconds);
        } else if (heartbeat.sentUs < requestedUs) {
            before.push_back(milliseconds);
        }
    }
    REQUIRE_FALSE(during.empty());
    REQUIRE_FALSE(before.empty());
    std::ranges::sort(during);
    std::ranges::sort(before);
    MESSAGE(std::format("screenshot of display 0: {}x{}, {} bytes of PNG, {:.0f} ms from the request to the reply",
                        screenshot->width, screenshot->height, screenshot->data.size(), captureMs));
    MESSAGE(std::format("{} round trips during the capture: median {:.1f} ms, p95 {:.1f} ms, max {:.1f} ms; "
                        "{} before it: median {:.1f} ms, p95 {:.1f} ms",
                        during.size(), nearestRank(during, 0.5), nearestRank(during, 0.95), during.back(),
                        before.size(), nearestRank(before, 0.5), nearestRank(before, 0.95)));
    CHECK(nearestRank(during, 0.95) < 50.0);
    CHECK(during.back() <= 150.0);

    request.requestId = 2;
    request.displayId = 4242;
    REQUIRE(session->send(request));
    const auto missing = bulkReply(listener, request.requestId, Clock::now() + 10s);
    REQUIRE(missing.has_value());
    const auto* refused = std::get_if<protocol::RequestFailed>(&*missing);
    REQUIRE(refused != nullptr);
    CHECK(refused->code == protocol::ErrorCode::NotFound);

    session->stop();
    REQUIRE(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));
    CHECK(serverGone(g_serial));
}

TEST_CASE("two devices stream at the same time (SES-002)")
{
    if (g_otherSerial.empty()) {
        MESSAGE("skipped; set GS_ADB_TEST_OTHER_SERIAL to a second device");
        return;
    }
    RecordingListener first;
    RecordingListener second;
    auto one = makeSession(g_serial, first, options(true));
    auto two = makeSession(g_otherSerial, second, options(true));
    one->start();
    two->start();
    REQUIRE(first.waitForPhase(SessionPhase::Streaming, 1, 30s));
    REQUIRE(second.waitForPhase(SessionPhase::Streaming, 1, 30s));
    CHECK(forwarded(g_serial));
    CHECK(forwarded(g_otherSerial));
    REQUIRE(first.waitForVideo(3, 15s));
    REQUIRE(second.waitForVideo(3, 15s));
    one->stop();
    two->stop();
    REQUIRE(first.waitForPhase(SessionPhase::Stopped, 1, 10s));
    REQUIRE(second.waitForPhase(SessionPhase::Stopped, 1, 10s));
    CHECK(serverGone(g_serial));
    CHECK(serverGone(g_otherSerial));
}

TEST_CASE("sessions opened and closed in a row leave nothing behind (SES-004)")
{
    DWORD baseline = 0;
    double baselineMemory = 0;
    for (int i = 0; i < g_cycles; ++i) {
        RecordingListener listener;
        auto session = makeSession(g_serial, listener, options(true));
        session->start();
        REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 1, 20s));
        REQUIRE(listener.waitForVideo(1, 15s));
        session->stop();
        REQUIRE(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));
        session.reset();
        if (i == 0) {
            baseline = handleCount();
            baselineMemory = privateMegabytes();
        }
    }
    const double memory = privateMegabytes();
    MESSAGE(std::format("{} sessions; handles {} after the first, {} after the last; private memory {:.1f} MB, then {:.1f} MB",
                        g_cycles, baseline, handleCount(), baselineMemory, memory));
    CHECK(sessionThreads() == 0);
    CHECK(handleCount() <= baseline + 16);
#if !defined(__SANITIZE_ADDRESS__)
    CHECK(memory <= baselineMemory + 8.0);
#endif
    CHECK_FALSE(forwarded(g_serial));
    CHECK(serverGone(g_serial));
}

TEST_CASE("launches in a row never race the server's readiness (SES-007)")
{
    int failures = 0;
    std::vector<double> streamingMs;
    for (int i = 0; i < g_launches; ++i) {
        RecordingListener listener;
        const auto start = Clock::now();
        auto session = makeSession(g_serial, listener, options(false));
        session->start();
        if (listener.waitForPhase(SessionPhase::Streaming, 1, 20s)) {
            streamingMs.push_back(millisecondsSince(start));
        } else {
            ++failures;
            const auto failed = listener.failure();
            MESSAGE(std::format("launch {} failed: {}", i, failed ? failed->detail : std::string("no answer")));
        }
        session->stop();
        REQUIRE(listener.waitForEnd(10s));
    }
    CHECK(failures == 0);
    CHECK(serverGone(g_serial));
    if (!streamingMs.empty()) {
        std::ranges::sort(streamingMs);
        MESSAGE(std::format("{} launches: start to streaming min {:.0f} ms, median {:.0f} ms, max {:.0f} ms", g_launches,
                            streamingMs.front(), streamingMs[streamingMs.size() / 2], streamingMs.back()));
    }
}

TEST_CASE("a session resumes after the adb server restarts (SES-006)")
{
    if (!g_allowRestart) {
        MESSAGE("skipped; set GS_ADB_TEST_RESTART=1 to kill and restart the adb server");
        return;
    }
    REQUIRE(std::filesystem::exists(g_adbExecutable));
    RecordingListener listener;
    auto session = makeSession(g_serial, listener, options(true));
    session->start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 1, 20s));
    REQUIRE(listener.waitForVideo(3, 15s));

    REQUIRE(killAdbServer());
    REQUIRE(listener.waitForPhase(SessionPhase::Reconnecting, 1, 15s));
    const auto restarted = Clock::now();
    REQUIRE(adb::ensureServer(g_adb, g_adbExecutable));
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 2, 30s));
    MESSAGE(std::format("resumed {:.0f} ms after the adb server was started again", millisecondsSince(restarted)));
    CHECK(listener.welcomes().back().resumed);

    const std::size_t before = listener.video().size();
    REQUIRE(listener.waitForVideo(before + 1, 15s));
    const auto video = listener.video();
    const auto descriptor = std::find_if(video.begin() + static_cast<std::ptrdiff_t>(before), video.end(),
                                         [](const VideoRecord& r) { return r.kind == protocol::MediaKind::Session; });
    CHECK(descriptor != video.end());

    session->stop();
    REQUIRE(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));
    CHECK(serverGone(g_serial));
}

TEST_CASE("a session runs over WiFi direct alone, to the serial's address and the announced port (SPEC 10.1)")
{
    if (!deviceAddress(0)) {
        MESSAGE("WiFi direct needs a device on wireless adb (serial ip:port); skipped");
        return;
    }
    const DirectRoute route{.devicePort = 0, .endpoint = std::nullopt};
    RecordingListener listener;
    auto session = std::make_unique<Session>(
        std::make_unique<AdbServerHost>(g_adb, g_serial, g_package, TransportChoice::WifiDirect, route), options(false),
        listener);
    const auto start = Clock::now();
    session->start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 1, 30s));
    MESSAGE(std::format("streaming over WiFi direct {:.0f} ms after start", millisecondsSince(start)));
    CHECK(listener.waitForHeartbeats(3, 10s));
    session->stop();
    REQUIRE(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));
    CHECK(serverGone(g_serial));
}

TEST_CASE("control frames cross a link losing 10% each way between the client and the real server, in order (WIF-004)")
{
    const auto device = deviceAddress(kDirectPort);
    if (!device) {
        MESSAGE("WiFi direct needs a device on wireless adb (serial ip:port); skipped");
        return;
    }
    LossyRelay relay(*device, 0.10);
    const DirectRoute route{.devicePort = kDirectPort, .endpoint = relay.endpoint()};
    AdbServerHost host(g_adb, g_serial, g_package, TransportChoice::WifiDirect, route);
    LaunchParams params;
    platform::randomBytes(params.token);
    std::array<std::uint8_t, 8> name{};
    platform::randomBytes(name);
    params.socketName = "gamescrcpy_" + platform::toHex(name);
    QuietHostEvents events;
    REQUIRE(host.start(params, events, {}));
    {
        auto link = host.transport(5s)->establish(params.token, "device-test", {});
        REQUIRE(link.has_value());
        FrameChannel& control = (*link)->control();
        constexpr std::uint32_t count = 200;
        for (std::uint32_t i = 0; i < count; ++i) {
            protocol::Ping ping;
            ping.nonce = i;
            ping.clientTimeUs = 1;
            REQUIRE(control.send(frameOf(ping)));
        }
        std::uint32_t next = 0;
        bool ordered = true;
        const auto start = Clock::now();
        while (next < count && Clock::now() - start < 30s) {
            const auto frame = control.receive();
            REQUIRE(frame.has_value());
            const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Control, *frame);
            REQUIRE(message.has_value());
            if (const auto* pong = std::get_if<protocol::Pong>(&*message)) {
                ordered = ordered && pong->nonce == next;
                ++next;
            }
        }
        CHECK(next == count);
        CHECK(ordered);
        MESSAGE(std::format("{} round trips through the lossy link in {:.0f} ms", next, millisecondsSince(start)));
        REQUIRE(control.send(frameOf(protocol::Goodbye{})));
        control.drain(Clock::now() + 2s);
    }
    host.stop(2s);
    CHECK(serverGone(g_serial));
}

TEST_CASE("WiFi direct chosen for a serial without an address takes the address the phone announces, or fails and says "
          "why, never using the adb tunnel (ADR-0055)")
{
    if (deviceAddress(0)) {
        MESSAGE("the serial holds the device's address; this case is for USB and emulator serials");
        return;
    }
    RecordingListener listener;
    auto session = std::make_unique<Session>(
        std::make_unique<AdbServerHost>(g_adb, g_serial, g_package, TransportChoice::WifiDirect), options(false),
        listener);
    session->start();
    const auto deadline = Clock::now() + 30s;
    while (Clock::now() < deadline && !listener.first(SessionPhase::Streaming) && !listener.waitForEnd(100ms)) {
    }
    const bool streamed = listener.first(SessionPhase::Streaming).has_value();
    if (streamed) {
        CHECK(listener.transports() == std::vector{TransportKind::WifiDirect});
    } else {
        REQUIRE(listener.waitForEnd(30s));
        const auto failed = listener.failure();
        REQUIRE(failed.has_value());
        CHECK((failed->kind == FailureKind::DirectNoAddress || failed->kind == FailureKind::DirectUnreachable));
        CHECK(listener.transports().empty());
    }
    session->stop();
    shell(g_serial, "pkill -f 'gamescrcpy.server.Ma[i]n'");
    CHECK(serverGone(g_serial));
}

TEST_CASE("WiFi direct without an answer fails in 1.5 s, and auto then streams over the adb tunnel (ADR-0055, SES-005)")
{
    auto silent = platform::UdpSocket::bind(platform::UdpEndpoint::loopback(0));
    REQUIRE(silent.has_value());
    const DirectRoute route{.devicePort = 0, .endpoint = platform::UdpEndpoint::loopback(silent->localPort())};

    RecordingListener direct;
    auto failing = std::make_unique<Session>(
        std::make_unique<AdbServerHost>(g_adb, g_serial, g_package, TransportChoice::WifiDirect, route), options(false),
        direct);
    failing->start();
    REQUIRE(direct.waitForEnd(30s));
    const auto failed = direct.failure();
    REQUIRE(failed.has_value());
    CHECK(failed->kind == FailureKind::DirectUnreachable);
    CHECK(failed->phase == SessionPhase::Handshaking);
    CHECK(direct.transports().empty());
    failing.reset();
    shell(g_serial, "pkill -f 'gamescrcpy.server.Ma[i]n'");
    CHECK(serverGone(g_serial));

    RecordingListener automatic;
    auto session = std::make_unique<Session>(
        std::make_unique<AdbServerHost>(g_adb, g_serial, g_package, TransportChoice::Auto, route), options(false),
        automatic);
    const auto start = Clock::now();
    session->start();
    REQUIRE(automatic.waitForPhase(SessionPhase::Streaming, 1, 30s));
    MESSAGE(std::format("streaming over the adb tunnel {:.0f} ms after start, WiFi direct given up on the way",
                        millisecondsSince(start)));
    CHECK(automatic.transports() == std::vector{TransportKind::AdbTunnel});
    CHECK(automatic.waitForHeartbeats(2, 10s));
    session->stop();
    REQUIRE(automatic.waitForPhase(SessionPhase::Stopped, 1, 10s));
    CHECK(serverGone(g_serial));
}

TEST_CASE("the client back on a new connection takes over at once, as a resumption (ADR-0056)")
{
    AdbServerHost host(g_adb, g_serial, g_package);
    LaunchParams params;
    platform::randomBytes(params.token);
    std::array<std::uint8_t, 8> name{};
    platform::randomBytes(name);
    params.socketName = "gamescrcpy_" + platform::toHex(name);
    QuietHostEvents events;
    REQUIRE(host.start(params, events, {}));
    const auto transport = host.transport(5s);
    auto first = transport->establish(params.token, "device-test", {});
    REQUIRE(first.has_value());
    CHECK_FALSE((*first)->welcome().resumed);
    auto second = transport->establish(params.token, "device-test", {});
    REQUIRE(second.has_value());
    CHECK((*second)->welcome().resumed);

    auto oldRead = std::async(std::launch::async, [&first] {
        while ((*first)->control().receive().has_value()) {
        }
    });
    if (oldRead.wait_for(5s) != std::future_status::ready) {
        (*first)->control().interrupt();
        FAIL("the server kept the old control connection");
    }
    oldRead.get();

    FrameChannel& control = (*second)->control();
    protocol::Ping ping;
    ping.nonce = 21;
    ping.clientTimeUs = 1;
    REQUIRE(control.send(frameOf(ping)));
    bool answered = false;
    while (!answered) {
        const auto frame = control.receive();
        REQUIRE(frame.has_value());
        const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Control, *frame);
        REQUIRE(message.has_value());
        if (const auto* pong = std::get_if<protocol::Pong>(&*message)) {
            CHECK(pong->nonce == 21);
            answered = true;
        }
    }
    REQUIRE(control.send(frameOf(protocol::Goodbye{})));
    control.drain(Clock::now() + 2s);
    host.stop(2s);
    CHECK(serverGone(g_serial));
}

TEST_CASE("the server's UDP port is open while the session lives and closed as soon as it ends (WIF-008)")
{
    auto silent = platform::UdpSocket::bind(platform::UdpEndpoint::loopback(0));
    REQUIRE(silent.has_value());
    const DirectRoute route{.devicePort = kDirectPort,
                            .endpoint = platform::UdpEndpoint::loopback(silent->localPort())};
    RecordingListener listener;
    auto session = std::make_unique<Session>(
        std::make_unique<AdbServerHost>(g_adb, g_serial, g_package, TransportChoice::Auto, route), options(false),
        listener);
    session->start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 1, 30s));
    CHECK(udpPortOpen(g_serial, kDirectPort));
    const auto stopped = Clock::now();
    session->stop();
    REQUIRE(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));
    const auto closed = closedAfter(g_serial, kDirectPort, stopped, stopped + 5s);
    REQUIRE(closed.has_value());
    MESSAGE(std::format("UDP port {} seen closed {:.0f} ms after the stop request", kDirectPort, *closed));
    CHECK(serverGone(g_serial));
}

TEST_CASE("after a lost connection the UDP port stays open while the server keeps the session, then closes (WIF-008)")
{
    auto silent = platform::UdpSocket::bind(platform::UdpEndpoint::loopback(0));
    REQUIRE(silent.has_value());
    const DirectRoute route{.devicePort = kDirectPort,
                            .endpoint = platform::UdpEndpoint::loopback(silent->localPort())};
    AdbServerHost host(g_adb, g_serial, g_package, TransportChoice::Auto, route);
    LaunchParams params;
    platform::randomBytes(params.token);
    std::array<std::uint8_t, 8> name{};
    platform::randomBytes(name);
    params.socketName = "gamescrcpy_" + platform::toHex(name);
    params.linger = 3s;
    QuietHostEvents events;
    REQUIRE(host.start(params, events, {}));
    {
        auto link = host.transport(5s)->establish(params.token, "device-test", {});
        REQUIRE(link.has_value());
        CHECK((*link)->kind() == TransportKind::AdbTunnel);
    }
    const auto lost = Clock::now();
    CHECK(udpPortOpen(g_serial, kDirectPort));
    const auto closed = closedAfter(g_serial, kDirectPort, lost, lost + params.linger + 5s);
    REQUIRE(closed.has_value());
    MESSAGE(std::format("UDP port {} seen closed {:.0f} ms after the connection was lost (linger {} ms)", kDirectPort,
                        *closed, params.linger.count()));
    CHECK(*closed >= 2500.0);
    host.stop(2s);
    CHECK(serverGone(g_serial));
}

int main(int argc, char** argv)
{
    g_serial = environment("GS_ADB_TEST_SERIAL");
    if (g_serial.empty()) {
        std::printf("gs_session_device_tests: skipped, set GS_ADB_TEST_SERIAL to a connected device\n");
        return 77;
    }
    g_otherSerial = environment("GS_ADB_TEST_OTHER_SERIAL");
    g_allowRestart = environment("GS_ADB_TEST_RESTART") == "1";
    g_adbExecutable = environment("GS_ADB_EXECUTABLE");
    if (g_adbExecutable.empty()) {
        g_adbExecutable = std::filesystem::path(environment("ANDROID_HOME")) / "platform-tools" / "adb.exe";
    }
    std::string apk = environment("GS_SERVER_APK");
    if (apk.empty()) {
        apk = GS_DEFAULT_SERVER_APK;
    }
    g_package = ServerPackage::load(apk);
    if (!g_package) {
        std::printf("gs_session_device_tests: no server build at %s; run gradlew :server:assembleRelease\n", apk.c_str());
        return 1;
    }
    if (const std::string cycles = environment("GS_SESSION_TEST_CYCLES"); !cycles.empty()) {
        g_cycles = std::max(1, std::atoi(cycles.c_str()));
    }
    if (const std::string launches = environment("GS_SESSION_TEST_LAUNCHES"); !launches.empty()) {
        g_launches = std::max(1, std::atoi(launches.c_str()));
    }
    StderrSink sink(environment("GS_SESSION_TEST_LOG") == "1");
    log::setSink(&sink);
    log::setMinimumLevel(log::Level::Debug);

    for (const std::string& serial : {g_serial, g_otherSerial}) {
        if (!serial.empty()) {
            shell(serial, "pkill -f 'gamescrcpy.server.Ma[i]n'; input keyevent KEYCODE_WAKEUP");
        }
    }

    doctest::Context context(argc, argv);
    const int result = context.run();
    log::setSink(nullptr);
    return result;
}
