// The C++ client against the Java server over WiFi direct (SPEC 10): DirectTransport talks to
// DirectInteropServer, the server's UdpTransport and SessionServer on a desktop JVM, so both
// implementations of the handshake and the reliable channel meet over real UDP. Emulators cannot
// take UDP from the host, and this needs no phone.
//
//   Needs android/server/build/interop/server.args (gradlew :server:writeInteropArguments, part of
//   gradlew build) and JAVA_HOME; reports "skipped" without them.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "DirectHandshake.h"
#include "DirectTransport.h"
#include "fakes/LossyRelay.h"
#include "fakes/RecordingListener.h"

#include "gs/platform/Crypto.h"
#include "gs/platform/UdpSocket.h"
#include "gs/protocol/Codec.h"
#include "gs/protocol/DirectMedia.h"
#include "gs/session/Session.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
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
using Clock = std::chrono::steady_clock;

namespace {

std::filesystem::path g_java;
std::filesystem::path g_arguments;

std::string environment(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, name);
    std::string text = value != nullptr ? value : "";
    std::free(value);
    return text;
}

Token randomToken()
{
    Token token{};
    platform::randomBytes(token);
    return token;
}

template <typename Value>
std::string failureOf(const Expected<Value, SessionFailure>& result)
{
    return result ? std::string() : std::format("kind {}: {}", static_cast<int>(result.error().kind), result.error().detail);
}

void warmUp(std::uint16_t port)
{
    const auto start = Clock::now();
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(port));
    std::string last;
    while (Clock::now() - start < 5s) {
        const auto refused = transport.establish(randomToken(), "warm-up", {});
        if (refused.has_value() || refused.error().kind == FailureKind::HandshakeRejected) {
            const std::chrono::duration<double, std::milli> took = Clock::now() - start;
            if (took >= detail::DirectHandshake::kGiveUpAfter) {
                MESSAGE(std::format("the cold Java server took {:.0f} ms to refuse a handshake", took.count()));
            }
            return;
        }
        last = failureOf(refused);
    }
    const std::chrono::duration<double, std::milli> took = Clock::now() - start;
    MESSAGE(std::format("the Java server refused no warm-up handshake in {:.0f} ms ({})", took.count(), last));
}

class JavaServer
{
public:
    explicit JavaServer(const Token& token, std::wstring_view arguments = {})
    {
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        HANDLE stdinRead = nullptr;
        HANDLE stdoutWrite = nullptr;
        REQUIRE(CreatePipe(&stdinRead, &m_stdin, &inherit, 0));
        REQUIRE(CreatePipe(&m_stdout, &stdoutWrite, &inherit, 0));
        SetHandleInformation(m_stdin, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(m_stdout, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdinRead;
        startup.hStdOutput = stdoutWrite;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        std::wstring command = std::format(L"\"{}\" \"@{}\"", g_java.wstring(), g_arguments.wstring());
        if (!arguments.empty()) {
            command += L' ';
            command += arguments;
        }
        PROCESS_INFORMATION process{};
        const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                            nullptr, &startup, &process);
        CloseHandle(stdinRead);
        CloseHandle(stdoutWrite);
        REQUIRE(started);
        CloseHandle(process.hThread);
        m_process = process.hProcess;
        m_reader = std::jthread([this] { readLines(); });

        const std::string line = "GS_TOKEN=" + platform::toHex(token) + "\n";
        DWORD written = 0;
        REQUIRE(WriteFile(m_stdin, line.data(), static_cast<DWORD>(line.size()), &written, nullptr));
    }

    ~JavaServer()
    {
        CloseHandle(m_stdin);
        if (WaitForSingleObject(m_process, 1000) != WAIT_OBJECT_0) {
            TerminateProcess(m_process, 1);
            WaitForSingleObject(m_process, 5000);
        }
        m_reader = {};
        CloseHandle(m_stdout);
        CloseHandle(m_process);
    }

    JavaServer(const JavaServer&) = delete;
    JavaServer& operator=(const JavaServer&) = delete;

    std::optional<std::string> line(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, timeout, [this] { return !m_lines.empty() || m_ended; }) || m_lines.empty()) {
            return std::nullopt;
        }
        std::string next = std::move(m_lines.front());
        m_lines.pop_front();
        return next;
    }

    bool exited(std::chrono::milliseconds timeout)
    {
        return WaitForSingleObject(m_process, static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
    }

    std::optional<std::uint16_t> tryReady()
    {
        const auto first = line(20s);
        const std::string prefix = std::format("GS_READY {} udp=", protocol::kProtocolVersion);
        if (!first || !first->starts_with(prefix)) {
            return std::nullopt;
        }
        const auto port = static_cast<std::uint16_t>(std::stoi(first->substr(prefix.size())));
        warmUp(port);
        return port;
    }

    std::uint16_t ready()
    {
        const auto first = line(20s);
        REQUIRE(first.has_value());
        INFO("the server said: " << *first);
        REQUIRE(first->starts_with(std::format("GS_READY {} udp=", protocol::kProtocolVersion)));
        const auto port = static_cast<std::uint16_t>(std::stoi(first->substr(first->find("udp=") + 4)));
        warmUp(port);
        return port;
    }

private:
    void readLines()
    {
        std::string pending;
        char chunk[512];
        DWORD read = 0;
        while (ReadFile(m_stdout, chunk, sizeof(chunk), &read, nullptr) && read != 0) {
            pending.append(chunk, read);
            for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
                std::string text = pending.substr(0, end);
                if (!text.empty() && text.back() == '\r') {
                    text.pop_back();
                }
                pending.erase(0, end + 1);
                {
                    const std::lock_guard lock(m_mutex);
                    m_lines.push_back(std::move(text));
                }
                m_changed.notify_all();
            }
        }
        {
            const std::lock_guard lock(m_mutex);
            m_ended = true;
        }
        m_changed.notify_all();
    }

    HANDLE m_stdin = nullptr;
    HANDLE m_stdout = nullptr;
    HANDLE m_process = nullptr;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::deque<std::string> m_lines;
    bool m_ended = false;
    std::jthread m_reader;
};

class InteropHost final : public ServerHost
{
public:
    explicit InteropHost(std::wstring arguments) : m_arguments(std::move(arguments)) {}

    Expected<void, SessionFailure> start(const LaunchParams& params, Events&,
                                         std::stop_token) override
    {
        m_server = std::make_unique<JavaServer>(params.token, m_arguments);
        const auto port = m_server->tryReady();
        if (!port) {
            SessionFailure failure;
            failure.kind = FailureKind::LaunchFailed;
            failure.detail = "the Java server did not get ready";
            return makeUnexpected(std::move(failure));
        }
        m_port = *port;
        return {};
    }

    Expected<platform::TcpStream, SessionFailure> connect() override
    {
        return makeUnexpected(SessionFailure{});
    }

    std::unique_ptr<Transport> transport(std::chrono::milliseconds) override
    {
        return std::make_unique<detail::DirectTransport>(platform::UdpEndpoint::loopback(m_port));
    }

    Expected<void, SessionFailure> restore(bool, std::chrono::steady_clock::time_point,
                                           std::stop_token) override
    {
        return {};
    }

    void stop(std::chrono::milliseconds ) override {}

private:
    std::wstring m_arguments;
    std::unique_ptr<JavaServer> m_server;
    std::uint16_t m_port = 0;
};

template <typename M>
std::vector<std::uint8_t> frameOf(const M& message)
{
    return *protocol::encodeFrame(message);
}

std::vector<std::uint8_t> ping(std::uint32_t nonce)
{
    protocol::Ping message;
    message.nonce = nonce;
    message.clientTimeUs = 1;
    return frameOf(message);
}

std::optional<protocol::Pong> nextPong(FrameChannel& control)
{
    while (true) {
        const auto frame = control.receive();
        if (!frame) {
            return std::nullopt;
        }
        const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Control, *frame);
        REQUIRE(message.has_value());
        if (const auto* pong = std::get_if<protocol::Pong>(&*message)) {
            return *pong;
        }
    }
}

}

TEST_CASE("the C++ client and the Java server handshake over UDP and talk on the reliable control stream")
{
    const Token token = randomToken();
    JavaServer server(token);
    const std::uint16_t port = server.ready();

    detail::DirectTransport transport(platform::UdpEndpoint::loopback(port));
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    CHECK((*link)->welcome().model == "Desktop JVM");
    CHECK_FALSE((*link)->welcome().resumed);

    FrameChannel& control = (*link)->control();
    REQUIRE(control.send(ping(7)));
    const auto pong = nextPong(control);
    REQUIRE(pong.has_value());
    CHECK(pong->nonce == 7);

    REQUIRE(control.send(frameOf(protocol::Goodbye{})));
    const auto before = Clock::now();
    CHECK_FALSE(nextPong(control).has_value());
    CHECK(Clock::now() - before < 1s);
    const auto ended = server.line(5s);
    REQUIRE(ended.has_value());
    CHECK(*ended == "GS_ENDED GOODBYE");
}

TEST_CASE("the Java server's UDP port is taken while the session lives and free as soon as it ends (WIF-008)")
{
    const Token token = randomToken();
    JavaServer server(token);
    const auto endpoint = platform::UdpEndpoint::loopback(server.ready());

    detail::DirectTransport transport(endpoint);
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    CHECK_FALSE(platform::UdpSocket::bind(endpoint).has_value());

    REQUIRE((*link)->control().send(frameOf(protocol::Goodbye{})));
    const auto goodbye = Clock::now();
    const auto ended = server.line(5s);
    REQUIRE(ended.has_value());
    CHECK(*ended == "GS_ENDED GOODBYE");
    CHECK(platform::UdpSocket::bind(endpoint).has_value());
    MESSAGE(std::format("the port was free {:.0f} ms after the Goodbye",
                        std::chrono::duration<double, std::milli>(Clock::now() - goodbye).count()));
    CHECK(server.exited(5s));
}

TEST_CASE("the C++ client back on a new UDP connection takes over at once, as a resumption (ADR-0056)")
{
    const Token token = randomToken();
    JavaServer server(token);
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(server.ready()));
    auto first = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(first.has_value(), failureOf(first));
    CHECK_FALSE((*first)->welcome().resumed);

    const auto before = Clock::now();
    auto second = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(second.has_value(), failureOf(second));
    CHECK((*second)->welcome().resumed);
    CHECK(Clock::now() - before < 1s);

    FrameChannel& control = (*second)->control();
    REQUIRE(control.send(ping(11)));
    const auto pong = nextPong(control);
    REQUIRE(pong.has_value());
    CHECK(pong->nonce == 11);
    REQUIRE(control.send(frameOf(protocol::Goodbye{})));
    CHECK_FALSE(nextPong(control).has_value());
    const auto ended = server.line(5s);
    REQUIRE(ended.has_value());
    CHECK(*ended == "GS_ENDED GOODBYE");
}

TEST_CASE("the Java server refuses the C++ client a wrong token")
{
    JavaServer server(randomToken());
    const std::uint16_t port = server.ready();
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(port));
    const auto link = transport.establish(randomToken(), "interop", {});
    REQUIRE_FALSE(link.has_value());
    CHECK_MESSAGE(link.error().kind == FailureKind::HandshakeRejected, failureOf(link));
    CHECK(link.error().code == protocol::ErrorCode::AuthenticationFailed);
}

TEST_CASE("across a link losing 10% each way the C++ client and the Java server keep every frame in order (WIF-004)")
{
    const Token token = randomToken();
    JavaServer server(token);
    LossyRelay relay(platform::UdpEndpoint::loopback(server.ready()), 0.10);

    detail::DirectTransport transport(relay.endpoint());
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    FrameChannel& control = (*link)->control();
    constexpr std::uint32_t count = 300;
    const auto start = Clock::now();
    for (std::uint32_t i = 0; i < count; ++i) {
        REQUIRE(control.send(ping(i)));
    }
    std::uint32_t inOrder = 0;
    while (inOrder < count) {
        const auto pong = nextPong(control);
        REQUIRE(pong.has_value());
        if (pong->nonce != inOrder) {
            break;
        }
        ++inOrder;
    }
    CHECK(inOrder == count);
    MESSAGE(std::format("{} round trips through 10% loss each way in {:.0f} ms", inOrder,
                        std::chrono::duration<double, std::milli>(Clock::now() - start).count()));
    REQUIRE(control.send(frameOf(protocol::Goodbye{})));
    control.drain(Clock::now() + 2s);
}

namespace {

constexpr std::uint32_t kInteropFrames = 120;
constexpr std::array<std::size_t, 5> kInteropSizes{200, 5'000, 30'000, 800, 12'000};

bool intactFrame(const protocol::MediaPacket& packet, std::size_t frameBytes)
{
    const auto index = static_cast<std::size_t>(packet.header.ptsUs / 1000);
    const std::size_t size = frameBytes != 0 ? frameBytes : kInteropSizes[index % kInteropSizes.size()];
    if (packet.header.kind != protocol::MediaKind::Frame || packet.header.session != 1 ||
        packet.header.keyFrame != (index % 30 == 0) || packet.payload.size() != size) {
        return false;
    }
    for (std::size_t j = 0; j < packet.payload.size(); ++j) {
        if (packet.payload[j] != static_cast<std::uint8_t>(index * 7 + j * 13)) {
            return false;
        }
    }
    return true;
}

constexpr std::uint32_t kInteropAudioPackets = 50;

bool intactAudio(const protocol::MediaPacket& packet)
{
    const auto index = static_cast<std::size_t>(packet.header.ptsUs / 20'000);
    if (packet.header.kind != protocol::MediaKind::Frame || packet.header.session != 1 ||
        packet.payload.size() != 60 + index % 40) {
        return false;
    }
    for (std::size_t j = 0; j < packet.payload.size(); ++j) {
        if (packet.payload[j] != static_cast<std::uint8_t>(index * 11 + j * 7)) {
            return false;
        }
    }
    return true;
}

struct MediaRun {
    std::uint32_t delivered = 0;
    std::uint64_t lost = 0;
    std::int64_t lastIndex = -1;
    bool intact = true;
    MediaChannel::LossCounts shards;
};

template <typename IndexOf, typename Intact>
MediaRun readPackets(MediaChannel& media, protocol::MediaChannel channel, std::chrono::milliseconds limit,
                     std::uint32_t count, IndexOf indexOf, Intact intact)
{
    std::jthread watchdog([&media, limit](std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        wake.wait_for(lock, stop, limit, [] { return false; });
        if (!stop.stop_requested()) {
            media.interrupt();
        }
    });
    MediaRun run;
    while (run.lastIndex + 1 < static_cast<std::int64_t>(count)) {
        const auto packet = media.receive();
        if (!packet) {
            break;
        }
        const auto decoded = protocol::decodeMediaPacket(channel, packet->bytes);
        run.intact = run.intact && decoded.has_value() && intact(*decoded);
        run.lost += packet->lostBefore;
        run.lastIndex = decoded ? indexOf(*decoded) : run.lastIndex + 1;
        ++run.delivered;
    }
    run.shards = media.takeLossCounts();
    return run;
}

MediaRun readFrames(MediaChannel& video, std::chrono::milliseconds limit, std::uint32_t frames = kInteropFrames,
                    std::size_t frameBytes = 0)
{
    return readPackets(
        video, protocol::MediaChannel::Video, limit, frames,
        [](const protocol::MediaPacket& packet) { return packet.header.ptsUs / 1000; },
        [frameBytes](const protocol::MediaPacket& packet) { return intactFrame(packet, frameBytes); });
}

MediaRun readAudio(MediaChannel& audio, std::chrono::milliseconds limit)
{
    return readPackets(
        audio, protocol::MediaChannel::Audio, limit, kInteropAudioPackets,
        [](const protocol::MediaPacket& packet) { return packet.header.ptsUs / 20'000; }, intactAudio);
}

}

TEST_CASE("the Java server's video reaches the C++ client whole over UDP (SPEC 10.5)")
{
    const Token token = randomToken();
    JavaServer server(token);
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(server.ready()));
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    auto video = (*link)->openMedia(protocol::MediaChannel::Video, {});
    REQUIRE(video.has_value());

    const MediaRun run = readFrames(**video, 10s);
    CHECK(run.delivered == kInteropFrames);
    CHECK(run.lost == 0);
    CHECK(run.intact);
    REQUIRE((*link)->control().send(frameOf(protocol::Goodbye{})));
    (*link)->control().drain(Clock::now() + 2s);
}

TEST_CASE("across 10% loss each way, the frames reaching the C++ client are whole and the others counted lost (SPEC 10.5)")
{
    const Token token = randomToken();
    JavaServer server(token);
    LossyRelay relay(platform::UdpEndpoint::loopback(server.ready()), 0.10);
    detail::DirectTransport transport(relay.endpoint());
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    auto video = (*link)->openMedia(protocol::MediaChannel::Video, {});
    REQUIRE(video.has_value());

    const MediaRun run = readFrames(**video, 5s);
    MESSAGE(std::format("{} frames whole, {} lost, {} shards rebuilt, {} missing", run.delivered, run.lost,
                        run.shards.rebuilt, run.shards.lost));
    CHECK(run.intact);
    CHECK(run.delivered > kInteropFrames / 2);
    CHECK(run.delivered + run.lost == static_cast<std::uint64_t>(run.lastIndex + 1));
    CHECK(run.shards.rebuilt > 0);
    REQUIRE((*link)->control().send(frameOf(protocol::Goodbye{})));
    (*link)->control().drain(Clock::now() + 2s);
}

TEST_CASE("the Java server's audio reaches the C++ client in order, each packet whole (SPEC 10.6)")
{
    const Token token = randomToken();
    JavaServer server(token);
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(server.ready()));
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    auto audio = (*link)->openMedia(protocol::MediaChannel::Audio, {});
    REQUIRE(audio.has_value());

    const MediaRun run = readAudio(**audio, 5s);
    CHECK(run.delivered == kInteropAudioPackets);
    CHECK(run.lost == 0);
    CHECK(run.intact);
    REQUIRE((*link)->control().send(frameOf(protocol::Goodbye{})));
    (*link)->control().drain(Clock::now() + 2s);
}

TEST_CASE("across 10% loss each way, the copy of the previous packet fills audio gaps (SPEC 10.6)")
{
    const Token token = randomToken();
    JavaServer server(token);
    LossyRelay relay(platform::UdpEndpoint::loopback(server.ready()), 0.10);
    detail::DirectTransport transport(relay.endpoint());
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    auto audio = (*link)->openMedia(protocol::MediaChannel::Audio, {});
    REQUIRE(audio.has_value());

    const MediaRun run = readAudio(**audio, 3s);
    const int dropped = relay.lostToClient(protocol::kAudioChannel);
    MESSAGE(std::format("{} of {} audio packets whole, {} lost, {} audio datagrams dropped", run.delivered,
                        kInteropAudioPackets, run.lost, dropped));
    CHECK(run.intact);
    CHECK(run.delivered + run.lost == static_cast<std::uint64_t>(run.lastIndex + 1));
    if (dropped > 0) {
        CHECK(run.lost < static_cast<std::uint64_t>(dropped));
    }
    REQUIRE((*link)->control().send(frameOf(protocol::Goodbye{})));
    (*link)->control().drain(Clock::now() + 2s);
}

TEST_CASE("frames of 500 KB, each sent in one burst, reach the C++ client whole")
{
    constexpr std::uint32_t kFrames = 30;
    constexpr std::size_t kBytes = 500'000;
    const Token token = randomToken();
    JavaServer server(token, std::format(L"frames={} frameBytes={} frameGapMillis=16", kFrames, kBytes));
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(server.ready()));
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    auto video = (*link)->openMedia(protocol::MediaChannel::Video, {});
    REQUIRE(video.has_value());

    const MediaRun run = readFrames(**video, 10s, kFrames, kBytes);
    MESSAGE(std::format("{} of {} frames whole, {} lost, {} shards rebuilt, {} missing", run.delivered, kFrames,
                        run.lost, run.shards.rebuilt, run.shards.lost));
    CHECK(run.delivered == kFrames);
    CHECK(run.lost == 0);
    CHECK(run.intact);
    REQUIRE((*link)->control().send(frameOf(protocol::Goodbye{})));
    (*link)->control().drain(Clock::now() + 2s);
}

TEST_CASE("the Java server closing the video channel ends it on the C++ client after its whole frames (ADR-0056)")
{
    constexpr std::uint64_t kFrames = 10;
    const Token token = randomToken();
    JavaServer server(token, std::format(L"closeVideoAfter={}", kFrames));
    detail::DirectTransport transport(platform::UdpEndpoint::loopback(server.ready()));
    auto link = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(link.has_value(), failureOf(link));
    auto video = (*link)->openMedia(protocol::MediaChannel::Video, {});
    REQUIRE(video.has_value());

    const auto start = Clock::now();
    const MediaRun run = readFrames(**video, 5s);
    CHECK(run.delivered == kFrames);
    CHECK(run.lost == 0);
    CHECK(run.intact);
    CHECK(Clock::now() - start < 3s);

    auto again = transport.establish(token, "interop", {});
    REQUIRE_MESSAGE(again.has_value(), failureOf(again));
    CHECK((*again)->welcome().resumed);
    CHECK((*again)->openMedia(protocol::MediaChannel::Video, {}).has_value());
    REQUIRE((*again)->control().send(frameOf(protocol::Goodbye{})));
    (*again)->control().drain(Clock::now() + 2s);
}

TEST_CASE("a session over WiFi direct resumes when the server closes its video channel, and streams again (ADR-0056)")
{
    RecordingListener listener;
    SessionOptions options;
    options.clientVersion = "interop";
    options.video = protocol::ConfigureVideo{};
    Session session(std::make_unique<InteropHost>(L"closeVideoAfter=5"), options, listener);
    session.start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming, 2, 15s));
    const auto reconnecting = listener.first(SessionPhase::Reconnecting);
    REQUIRE(reconnecting.has_value());
    REQUIRE(reconnecting->failure.has_value());
    CHECK(reconnecting->failure->kind == FailureKind::ConnectionLost);
    const auto welcomes = listener.welcomes();
    REQUIRE(welcomes.size() >= 2);
    CHECK_FALSE(welcomes[0].resumed);
    CHECK(welcomes[1].resumed);
    CHECK(listener.transports().front() == TransportKind::WifiDirect);
    CHECK(listener.waitForVideo(10, 10s));
    session.stop();
    CHECK(listener.waitForPhase(SessionPhase::Stopped, 1, 10s));
}

int main(int argc, char** argv)
{
    g_arguments = GS_INTEROP_ARGUMENTS;
    const std::string javaHome = environment("JAVA_HOME");
    g_java = std::filesystem::path(javaHome) / "bin" / "java.exe";
    if (!std::filesystem::exists(g_arguments) || javaHome.empty() || !std::filesystem::exists(g_java)) {
        std::printf("gs_direct_interop_tests: skipped, needs %s (gradlew build) and JAVA_HOME\n",
                    g_arguments.string().c_str());
        return 77;
    }
    doctest::Context context(argc, argv);
    return context.run();
}
