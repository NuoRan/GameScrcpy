#include "FakeAdbServer.h"

#include "gs/adb/AdbClient.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

using namespace gs::adb;
using gs::adb::test::closedPort;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;
using gs::adb::test::FakeStorage;

namespace {

std::string le32(std::uint32_t value)
{
    std::string bytes(4, '\0');
    for (int i = 0; i < 4; ++i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<char>(value >> (8 * i));
    }
    return bytes;
}

std::uint32_t readLe32(FakeConnection& c)
{
    const std::string bytes = c.read(4);
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
    }
    return value;
}

std::string shellPacket(char id, std::string_view data)
{
    return std::string(1, id) + le32(static_cast<std::uint32_t>(data.size())) + std::string(data);
}

void acceptTransport(FakeConnection& c, std::string_view serial = "emulator-5556")
{
    CHECK(c.readRequest() == "host:transport:" + std::string(serial));
    c.write("OKAY");
}

class TempFolder
{
public:
    TempFolder()
    {
        static std::atomic<int> count{0};
        m_path = std::filesystem::temp_directory_path() /
                 std::format("gs-adb-tests-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(),
                             count.fetch_add(1));
        std::filesystem::create_directories(m_path);
    }
    ~TempFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string pattern(std::size_t size)
{
    std::string content(size, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        content[i] = static_cast<char>((i * 31 + 7) & 0xFFU);
    }
    return content;
}

const RemoteEntry* entryNamed(const std::vector<RemoteEntry>& entries, std::string_view name)
{
    const auto found = std::ranges::find(entries, name, &RemoteEntry::name);
    return found == entries.end() ? nullptr : &*found;
}

bool asked(const FakeStorage& storage, std::string_view request)
{
    const std::vector<std::string> requests = storage.requests();
    return std::ranges::find(requests, request) != requests.end();
}

std::filesystem::path partialOf(const std::filesystem::path& local)
{
    std::filesystem::path partial = local;
    partial += L".gs-part";
    return partial;
}

}

TEST_CASE("adb client: server port from ANDROID_ADB_SERVER_PORT")
{
    CHECK(AdbClient::portFromSetting("5038") == 5038);
    CHECK(AdbClient::portFromSetting("1") == 1);
    CHECK(AdbClient::portFromSetting("65535") == 65535);
    CHECK(AdbClient::portFromSetting("") == AdbClient::kDefaultPort);
    CHECK(AdbClient::portFromSetting("0") == AdbClient::kDefaultPort);
    CHECK(AdbClient::portFromSetting("65536") == AdbClient::kDefaultPort);
    CHECK(AdbClient::portFromSetting("-1") == AdbClient::kDefaultPort);
    CHECK(AdbClient::portFromSetting("5038x") == AdbClient::kDefaultPort);
    CHECK(AdbClient::portFromSetting(" 5038") == AdbClient::kDefaultPort);
}

TEST_CASE("adb client: server version")
{
    FakeAdbServer server([](FakeConnection& c) {
        CHECK(c.readRequest() == "host:version");
        c.write("OKAY" + FakeConnection::prefixed("0029"));
    });
    const auto version = AdbClient(server.port()).serverVersion();
    REQUIRE(version);
    CHECK(*version == 41);
}

TEST_CASE("adb client: device list")
{
    FakeAdbServer server([](FakeConnection& c) {
        CHECK(c.readRequest() == "host:devices-l");
        c.write("OKAY" + FakeConnection::prefixed("emulator-5556          device product:p model:m device:d "
                                                  "transport_id:4\nR58M\tunauthorized transport_id:5\n"));
    });
    const auto devices = AdbClient(server.port()).devices();
    REQUIRE(devices);
    REQUIRE(devices->size() == 2);
    CHECK((*devices)[0].model == "m");
    CHECK((*devices)[1].state == DeviceState::Unauthorized);
}

TEST_CASE("adb client: FAIL replies carry the reason, possibly empty")
{
    std::string reply;
    FakeAdbServer server([&](FakeConnection& c) {
        (void)c.readRequest();
        c.write(reply);
    });
    const AdbClient client(server.port());

    reply = "FAIL" + FakeConnection::prefixed("device 'x' not found");
    auto result = client.serverVersion();
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == AdbErrorKind::Rejected);
    CHECK(result.error().message == "device 'x' not found");

    reply = "FAIL0000";
    result = client.serverVersion();
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == AdbErrorKind::Rejected);
    CHECK(result.error().message.empty());
}

TEST_CASE("adb client: malformed and truncated replies")
{
    std::string reply;
    FakeAdbServer server([&](FakeConnection& c) {
        (void)c.readRequest();
        c.write(reply);
    });
    const AdbClient client(server.port());

    reply = "WHAT";
    CHECK(client.serverVersion().error().kind == AdbErrorKind::Protocol);
    reply = "OKAYzz12";
    CHECK(client.serverVersion().error().kind == AdbErrorKind::Protocol);
    reply = "OKAY0004zz";
    CHECK(client.serverVersion().error().kind == AdbErrorKind::Io);
    reply = "OKAY0002zz";
    CHECK(client.serverVersion().error().kind == AdbErrorKind::Protocol);
    reply = "OK";
    CHECK(client.serverVersion().error().kind == AdbErrorKind::Io);
    reply = "FAIL00";
    CHECK(client.serverVersion().error().kind == AdbErrorKind::Io);
}

TEST_CASE("adb client: no server fails at once")
{
    const AdbClient client(closedPort());
    const auto start = std::chrono::steady_clock::now();
    const auto result = client.serverVersion();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == AdbErrorKind::ServerUnavailable);
    MESSAGE("refused connection took " << std::chrono::duration<double, std::milli>(elapsed).count() << " ms");
    CHECK(elapsed < std::chrono::milliseconds(300));
}

TEST_CASE("adb client: features")
{
    FakeAdbServer server([](FakeConnection& c) {
        CHECK(c.readRequest() == "host-serial:emulator-5556:features");
        c.write("OKAY" + FakeConnection::prefixed("shell_v2,cmd,,stat_v2"));
    });
    const auto features = AdbClient(server.port()).features("emulator-5556");
    REQUIRE(features);
    CHECK(*features == std::vector<std::string>{"shell_v2", "cmd", "stat_v2"});
}

TEST_CASE("adb client: forward to a free local port and remove it")
{
    std::string reply;
    std::string expected;
    FakeAdbServer server([&](FakeConnection& c) {
        CHECK(c.readRequest() == expected);
        c.write(reply);
    });
    const AdbClient client(server.port());

    expected = "host-serial:emulator-5556:forward:tcp:0;localabstract:gamescrcpy";
    reply = "OKAYOKAY" + FakeConnection::prefixed("2963");
    auto port = client.forward("emulator-5556", "localabstract:gamescrcpy");
    REQUIRE(port);
    CHECK(*port == 2963);

    reply = "OKAYOKAY" + FakeConnection::prefixed("0");
    CHECK(client.forward("emulator-5556", "localabstract:gamescrcpy").error().kind == AdbErrorKind::Protocol);
    reply = "OKAYOKAY" + FakeConnection::prefixed("70000");
    CHECK(client.forward("emulator-5556", "localabstract:gamescrcpy").error().kind == AdbErrorKind::Protocol);
    reply = "FAIL0000";
    CHECK(client.forward("emulator-5556", "localabstract:gamescrcpy").error().kind == AdbErrorKind::Rejected);
    reply = "OKAYFAIL" + FakeConnection::prefixed("cannot bind");
    CHECK(client.forward("emulator-5556", "localabstract:gamescrcpy").error().message == "cannot bind");

    expected = "host-serial:emulator-5556:killforward:tcp:2963";
    reply = "OKAYOKAY";
    CHECK(client.removeForward("emulator-5556", 2963));
    reply = "FAIL" + FakeConnection::prefixed("listener 'tcp:2963' not found");
    CHECK(client.removeForward("emulator-5556", 2963).error().kind == AdbErrorKind::Rejected);
}

TEST_CASE("adb client: forward and reverse lists")
{
    FakeAdbServer server([](FakeConnection& c) {
        const std::string first = c.readRequest();
        if (first == "host:list-forward") {
            c.write("OKAY" + FakeConnection::prefixed("emulator-5556 tcp:2963 localabstract:gamescrcpy\n"));
            return;
        }
        CHECK(first == "host:transport:emulator-5556");
        c.write("OKAY");
        CHECK(c.readRequest() == "reverse:list-forward");
        c.write("OKAY" + FakeConnection::prefixed("host-17 localabstract:gs_rev tcp:27999\n"));
    });
    const AdbClient client(server.port());

    const auto forwards = client.forwards();
    REQUIRE(forwards);
    REQUIRE(forwards->size() == 1);
    CHECK((*forwards)[0] == ForwardEntry{"emulator-5556", "tcp:2963", "localabstract:gamescrcpy"});

    const auto reverses = client.reverses("emulator-5556");
    REQUIRE(reverses);
    REQUIRE(reverses->size() == 1);
    CHECK((*reverses)[0] == ForwardEntry{"host-17", "tcp:27999", "localabstract:gs_rev"});
}

TEST_CASE("adb client: wireless connect replies")
{
    std::string reply;
    std::string expected;
    FakeAdbServer server([&](FakeConnection& c) {
        CHECK(c.readRequest() == expected);
        c.write(reply);
    });
    const AdbClient client(server.port());

    expected = "host:connect:192.168.1.20:5555";
    reply = "OKAY" + FakeConnection::prefixed("connected to 192.168.1.20:5555");
    CHECK(client.connect("192.168.1.20:5555").value() == "192.168.1.20:5555");
    reply = "OKAY" + FakeConnection::prefixed("already connected to 192.168.1.20:5555\n");
    CHECK(client.connect("192.168.1.20:5555").value() == "192.168.1.20:5555");
    expected = "host:connect:192.168.1.20";
    reply = "OKAY" + FakeConnection::prefixed("connected to 192.168.1.20:5555");
    CHECK(client.connect("192.168.1.20").value() == "192.168.1.20:5555");
    expected = "host:connect:192.168.1.20:5555";
    reply = "OKAY" + FakeConnection::prefixed("failed to connect to '192.168.1.20:5555': Connection refused");
    const auto refused = client.connect("192.168.1.20:5555");
    REQUIRE_FALSE(refused);
    CHECK(refused.error().kind == AdbErrorKind::Rejected);
    CHECK(refused.error().message.starts_with("failed to connect"));
    reply = "OKAYFAIL" + FakeConnection::prefixed("cannot connect");
    CHECK(client.connect("192.168.1.20:5555").error().message == "cannot connect");

    expected = "host:disconnect:192.168.1.20:5555";
    reply = "OKAY" + FakeConnection::prefixed("disconnected 192.168.1.20:5555");
    CHECK(client.disconnect("192.168.1.20:5555"));
    reply = "FAIL" + FakeConnection::prefixed("no such device '192.168.1.20:5555'");
    CHECK(client.disconnect("192.168.1.20:5555").error().kind == AdbErrorKind::Rejected);
}

TEST_CASE("adb client: wireless pairing replies")
{
    std::string reply;
    FakeAdbServer server([&](FakeConnection& c) {
        CHECK(c.readRequest() == "host:pair:123456:192.168.1.20:37123");
        c.write(reply);
    });
    const AdbClient client(server.port());

    reply = "OKAY" +
            FakeConnection::prefixed("Successfully paired to 192.168.1.20:37123 [guid=adb-R58M123ABC-AbCdEf]");
    CHECK(client.pair("192.168.1.20:37123", "123456").value().starts_with("Successfully paired to"));

    reply = "OKAYFAIL" + FakeConnection::prefixed("Failed: Unable to start pairing client.");
    const auto unreachable = client.pair("192.168.1.20:37123", "123456");
    REQUIRE_FALSE(unreachable);
    CHECK(unreachable.error().kind == AdbErrorKind::Rejected);
    CHECK(unreachable.error().message == "Failed: Unable to start pairing client.");

    reply = "FAIL" + FakeConnection::prefixed("unknown host service");
    CHECK(client.pair("192.168.1.20:37123", "123456").error().kind == AdbErrorKind::Rejected);
    reply = "OKAY" + FakeConnection::prefixed("something else");
    CHECK(client.pair("192.168.1.20:37123", "123456").error().kind == AdbErrorKind::Rejected);
}

TEST_CASE("adb client: a stop request ends a wireless wait at once")
{
    std::promise<void> asked;
    FakeAdbServer server([&](FakeConnection& c) {
        CHECK(c.readRequest() == "host:pair:123456:192.168.1.20:37123");
        c.write("OKAY");
        asked.set_value();
        CHECK(c.waitForClientClose());
    });
    const AdbClient client(server.port());
    std::stop_source stop;
    auto pairing = std::async(std::launch::async,
                              [&] { return client.pair("192.168.1.20:37123", "123456", stop.get_token()); });
    asked.get_future().wait();
    const auto start = std::chrono::steady_clock::now();
    stop.request_stop();
    const auto result = pairing.get();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == AdbErrorKind::Io);
}

TEST_CASE("adb client: wireless services that mDNS discovery sees")
{
    FakeAdbServer server([](FakeConnection& c) {
        CHECK(c.readRequest() == "host:mdns:services");
        c.write("OKAY" +
                FakeConnection::prefixed("adb-FAKE0001-AbCdEf\t_adb-tls-pairing._tcp\t192.168.31.68:37123\n"));
    });
    const auto services = AdbClient(server.port()).wirelessServices();
    REQUIRE(services);
    REQUIRE(services->size() == 1);
    CHECK((*services)[0] ==
          WirelessService{"adb-FAKE0001-AbCdEf", WirelessService::Kind::Pairing, "192.168.31.68:37123"});
}

TEST_CASE("adb client: reverse and its removal")
{
    std::string second;
    std::string expected;
    FakeAdbServer server([&](FakeConnection& c) {
        acceptTransport(c);
        CHECK(c.readRequest() == expected);
        c.write("OKAY" + second);
    });
    const AdbClient client(server.port());

    expected = "reverse:forward:localabstract:gs_rev;tcp:27999";
    second = "OKAY";
    CHECK(client.reverse("emulator-5556", "localabstract:gs_rev", 27999));

    expected = "reverse:killforward:localabstract:gs_rev";
    CHECK(client.removeReverse("emulator-5556", "localabstract:gs_rev"));
    second = "FAIL" + FakeConnection::prefixed("listener 'localabstract:gs_rev' not found");
    const auto removed = client.removeReverse("emulator-5556", "localabstract:gs_rev");
    REQUIRE_FALSE(removed);
    CHECK(removed.error().message == "listener 'localabstract:gs_rev' not found");
}

TEST_CASE("adb client: shell collects output across split writes")
{
    FakeAdbServer server([](FakeConnection& c) {
        acceptTransport(c);
        CHECK(c.readRequest() == "shell,v2,raw:echo out; echo err 1>&2; exit 3");
        const std::string all = "OKAY" + shellPacket(1, "out\n") + shellPacket(2, "err\n") + shellPacket(1, "more") +
                                shellPacket(3, std::string(1, '\x03'));
        for (std::size_t i = 0; i < all.size(); i += 3) {
            c.write(std::string_view(all).substr(i, 3));
        }
    });
    const auto result = AdbClient(server.port()).shell("emulator-5556", "echo out; echo err 1>&2; exit 3");
    REQUIRE(result);
    CHECK(result->out == "out\nmore");
    CHECK(result->err == "err\n");
    CHECK(result->exitCode == 3);
}

TEST_CASE("adb client: shell failures")
{
    std::string reply;
    FakeAdbServer server([&](FakeConnection& c) {
        acceptTransport(c);
        (void)c.readRequest();
        c.write(reply);
    });
    const AdbClient client(server.port());

    reply = "OKAY" + shellPacket(1, "partial");
    CHECK(client.shell("emulator-5556", "x").error().kind == AdbErrorKind::Io);
    reply = "OKAY" + shellPacket(3, "");
    CHECK(client.shell("emulator-5556", "x").error().kind == AdbErrorKind::Protocol);
    reply = "OKAY" + std::string("\x01\xFF\xFF\xFF\x7F", 5);
    CHECK(client.shell("emulator-5556", "x").error().kind == AdbErrorKind::Protocol);
    reply = "FAIL" + FakeConnection::prefixed("closed");
    CHECK(client.shell("emulator-5556", "x").error().message == "closed");
}

TEST_CASE("adb client: an unknown device is rejected at the transport switch")
{
    FakeAdbServer server([](FakeConnection& c) {
        CHECK(c.readRequest() == "host:transport:nosuchdevice");
        c.write("FAIL" + FakeConnection::prefixed("device 'nosuchdevice' not found"));
    });
    const auto result = AdbClient(server.port()).shell("nosuchdevice", "true");
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == AdbErrorKind::Rejected);
    CHECK(result.error().message == "device 'nosuchdevice' not found");
}

TEST_CASE("adb client: requests longer than the length prefix allows are refused")
{
    FakeAdbServer server([](FakeConnection& c) {
        acceptTransport(c);
        CHECK(c.readRequest().empty());
    });
    const auto result = AdbClient(server.port()).shell("emulator-5556", std::string(70000, 'x'));
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == AdbErrorKind::Protocol);
}

TEST_CASE("adb client: push sends SEND, 64 KiB DATA chunks and DONE")
{
    std::vector<std::uint8_t> content(150000);
    for (std::size_t i = 0; i < content.size(); ++i) {
        content[i] = static_cast<std::uint8_t>(i * 31 + 7);
    }
    std::string sendTarget;
    std::vector<std::uint32_t> chunkSizes;
    std::string received;
    bool quit = false;
    std::string pushReply = "OKAY" + le32(0);
    FakeAdbServer server([&](FakeConnection& c) {
        acceptTransport(c);
        CHECK(c.readRequest() == "sync:");
        c.write("OKAY");
        CHECK(c.read(4) == "SEND");
        sendTarget = c.read(readLe32(c));
        for (;;) {
            const std::string id = c.read(4);
            const std::uint32_t value = readLe32(c);
            if (id == "DONE") {
                CHECK(value > 0);
                break;
            }
            if (id != "DATA") {
                FAIL_CHECK("unexpected sync id " << id);
                return;
            }
            chunkSizes.push_back(value);
            received += c.read(value);
        }
        c.write(pushReply);
        quit = c.read(8) == std::string("QUIT") + le32(0);
    });
    const AdbClient client(server.port());

    REQUIRE(client.push("emulator-5556", content, "/data/local/tmp/gs-server.jar", 0644));
    REQUIRE(server.waitForHandled(1));
    CHECK(sendTarget == "/data/local/tmp/gs-server.jar,33188");
    CHECK(chunkSizes == std::vector<std::uint32_t>{65536, 65536, 18928});
    CHECK(received == std::string(content.begin(), content.end()));
    CHECK(quit);

    chunkSizes.clear();
    received.clear();
    REQUIRE(client.push("emulator-5556", {}, "/data/local/tmp/empty", 0600));
    REQUIRE(server.waitForHandled(2));
    CHECK(sendTarget == "/data/local/tmp/empty,33152");
    CHECK(chunkSizes.empty());

    const std::string reason = "couldn't create file: Permission denied";
    pushReply = "FAIL" + le32(static_cast<std::uint32_t>(reason.size())) + reason;
    const auto denied = client.push("emulator-5556", content, "/system/x", 0644);
    REQUIRE_FALSE(denied);
    CHECK(denied.error().kind == AdbErrorKind::Rejected);
    CHECK(denied.error().message == reason);
}

TEST_CASE("adb client: stat of present and missing files")
{
    std::string reply;
    std::string path;
    FakeAdbServer server([&](FakeConnection& c) {
        acceptTransport(c);
        CHECK(c.readRequest() == "sync:");
        c.write("OKAY");
        CHECK(c.read(4) == "STAT");
        path = c.read(readLe32(c));
        c.write(reply);
        (void)c.read(8);
    });
    const AdbClient client(server.port());

    reply = "STAT" + le32(0100644) + le32(1234) + le32(1700000000);
    auto info = client.stat("emulator-5556", "/data/local/tmp/gs-server.jar");
    REQUIRE(info);
    REQUIRE(info->has_value());
    REQUIRE(server.waitForHandled(1));
    CHECK(path == "/data/local/tmp/gs-server.jar");
    CHECK((*info)->mode == 0100644);
    CHECK((*info)->size == 1234);
    CHECK((*info)->mtime == 1700000000);

    reply = "STAT" + le32(0) + le32(0) + le32(0);
    info = client.stat("emulator-5556", "/data/local/tmp/missing");
    REQUIRE(info);
    CHECK_FALSE(info->has_value());

    reply = "FAIL" + le32(0) + le32(0) + le32(0);
    CHECK(client.stat("emulator-5556", "/x").error().kind == AdbErrorKind::Protocol);
    CHECK(client.stat("emulator-5556", "").error().kind == AdbErrorKind::Protocol);
}

TEST_CASE("adb client: spawned process stdin, stdout and exit")
{
    std::string stdinData;
    bool stdinClosed = false;
    FakeAdbServer server([&](FakeConnection& c) {
        acceptTransport(c);
        CHECK(c.readRequest() == "shell,v2,raw:cat");
        c.write("OKAY");
        for (;;) {
            const std::string header = c.read(5);
            if (header.size() < 5) {
                break;
            }
            const auto length = static_cast<std::uint32_t>(static_cast<unsigned char>(header[1]));
            if (header[0] == 4) {
                stdinClosed = true;
                break;
            }
            stdinData += c.read(length);
        }
        c.write(shellPacket(1, stdinData) + shellPacket(3, std::string(1, '\0')));
    });
    auto process = AdbClient(server.port()).spawn("emulator-5556", "cat");
    REQUIRE(process);
    const std::string token = "token-1234\n";
    REQUIRE(process->writeStdin(std::span(reinterpret_cast<const std::uint8_t*>(token.data()), token.size())));
    REQUIRE(process->closeStdin());
    const auto out = process->read();
    REQUIRE(out);
    CHECK(out->id == 1);
    CHECK(std::string(out->data.begin(), out->data.end()) == token);
    const auto exit = process->read();
    REQUIRE(exit);
    CHECK(exit->id == 3);
    CHECK_FALSE(process->read());
    REQUIRE(server.waitForHandled(1));
    CHECK(stdinData == token);
    CHECK(stdinClosed);
}

TEST_CASE("adb client: interrupt wakes a blocked read at once")
{
    FakeAdbServer server([](FakeConnection& c) {
        acceptTransport(c);
        (void)c.readRequest();
        c.write("OKAY");
        CHECK(c.waitForClientClose());
    });
    auto process = AdbClient(server.port()).spawn("emulator-5556", "sleep 100");
    REQUIRE(process);
    auto reader = std::async(std::launch::async, [&] { return process->read().has_value(); });
    CHECK(reader.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
    const auto start = std::chrono::steady_clock::now();
    process->interrupt();
    REQUIRE(reader.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_FALSE(reader.get());
    MESSAGE("interrupt took " << std::chrono::duration<double, std::milli>(elapsed).count() << " ms");
    CHECK(elapsed < std::chrono::milliseconds(200));
}

TEST_CASE("adb client: a folder is listed with LIS2 when the phone has ls_v2")
{
    FakeStorage storage;
    storage.addFolder("/sdcard/DCIM", 1700000100);
    storage.addFile("/sdcard/notes.txt", "hello", 1700000200);
    storage.addFile("/sdcard/movie.mkv", "x", 1700000300);
    storage.setReportedSize("/sdcard/movie.mkv", 5000000000ULL);
    storage.addFile("/sdcard/DCIM/inner.jpg", "jpg");
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });

    const auto entries = AdbClient(server.port()).list("emulator-5556", "/sdcard");
    REQUIRE(entries);
    CHECK(entries->size() == 3);
    const RemoteEntry* dcim = entryNamed(*entries, "DCIM");
    const RemoteEntry* notes = entryNamed(*entries, "notes.txt");
    const RemoteEntry* movie = entryNamed(*entries, "movie.mkv");
    REQUIRE(dcim != nullptr);
    REQUIRE(notes != nullptr);
    REQUIRE(movie != nullptr);
    CHECK(dcim->directory);
    CHECK(dcim->modifiedSeconds == 1700000100);
    CHECK_FALSE(notes->directory);
    CHECK(notes->size == 5);
    CHECK(notes->mode == FakeStorage::kFileMode);
    CHECK(notes->modifiedSeconds == 1700000200);
    CHECK(movie->size == 5000000000ULL);
    CHECK(entryNamed(*entries, ".") == nullptr);
    CHECK(entryNamed(*entries, "..") == nullptr);
    CHECK(entryNamed(*entries, "inner.jpg") == nullptr);
    CHECK(asked(storage, "features"));
    CHECK(asked(storage, "LIS2 /sdcard"));
}

TEST_CASE("adb client: phones without ls_v2 are listed with LIST")
{
    FakeStorage storage("emulator-5556", "shell_v2,cmd,stat_v2");
    storage.addFolder("/sdcard/Music", 1700000100);
    storage.addFile("/sdcard/movie.mkv", "x", 1700000300);
    storage.setReportedSize("/sdcard/movie.mkv", 5000000000ULL);
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });

    const auto entries = AdbClient(server.port()).list("emulator-5556", "/sdcard");
    REQUIRE(entries);
    REQUIRE(entries->size() == 2);
    const RemoteEntry* music = entryNamed(*entries, "Music");
    const RemoteEntry* movie = entryNamed(*entries, "movie.mkv");
    REQUIRE(music != nullptr);
    REQUIRE(movie != nullptr);
    CHECK(music->directory);
    CHECK(movie->size == (5000000000ULL & 0xFFFFFFFFULL));
    CHECK(movie->modifiedSeconds == 1700000300);
    CHECK(asked(storage, "LIST /sdcard"));
    CHECK_FALSE(asked(storage, "LIS2 /sdcard"));
}

TEST_CASE("adb client: a failed list carries the phone's reason")
{
    FakeAdbServer server([](FakeConnection& c) {
        const std::string first = c.readRequest();
        if (first == "host-serial:emulator-5556:features") {
            c.write("OKAY" + FakeConnection::prefixed("shell_v2,ls_v2"));
            return;
        }
        CHECK(first == "host:transport:emulator-5556");
        c.write("OKAY");
        CHECK(c.readRequest() == "sync:");
        c.write("OKAY");
        CHECK(c.read(4) == "LIS2");
        (void)c.read(readLe32(c));
        c.write("FAIL" + le32(17) + "Permission denied");
    });
    const auto entries = AdbClient(server.port()).list("emulator-5556", "/sdcard/Android/data");
    REQUIRE_FALSE(entries);
    CHECK(entries.error().kind == AdbErrorKind::Rejected);
    CHECK(entries.error().message == "Permission denied");
}

TEST_CASE("adb client: a file is pulled in chunks with progress over RCV2, or RECV on older phones")
{
    const std::string content = pattern(200000);
    FakeStorage storage;
    storage.addFile("/sdcard/Download/movie.bin", content);
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    const AdbClient client(server.port());
    const TempFolder temp;

    const std::filesystem::path local = temp.path() / "movie.bin";
    std::vector<std::pair<std::uint64_t, std::uint64_t>> reports;
    REQUIRE(client.pullFile("emulator-5556", "/sdcard/Download/movie.bin", local,
                            [&reports](std::uint64_t done, std::uint64_t total) { reports.emplace_back(done, total); }));
    CHECK(readFile(local) == content);
    CHECK_FALSE(std::filesystem::exists(partialOf(local)));
    REQUIRE(reports.size() == 5);
    CHECK(reports.front() == std::pair<std::uint64_t, std::uint64_t>(0, 200000));
    CHECK(reports[1].first == 65536);
    CHECK(reports.back() == std::pair<std::uint64_t, std::uint64_t>(200000, 200000));
    CHECK(std::ranges::is_sorted(reports));
    CHECK(asked(storage, "STA2 /sdcard/Download/movie.bin"));
    CHECK(asked(storage, "RCV2 /sdcard/Download/movie.bin"));

    storage.setFeatures("shell_v2,cmd");
    const std::filesystem::path again = temp.path() / "again.bin";
    REQUIRE(client.pullFile("emulator-5556", "/sdcard/Download/movie.bin", again));
    CHECK(readFile(again) == content);
    CHECK(asked(storage, "STAT /sdcard/Download/movie.bin"));
    CHECK(asked(storage, "RECV /sdcard/Download/movie.bin"));
}

TEST_CASE("adb client: pulling a missing file or a folder fails without leaving a file")
{
    FakeStorage storage;
    storage.addFolder("/sdcard/DCIM");
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    const AdbClient client(server.port());
    const TempFolder temp;
    const std::filesystem::path local = temp.path() / "x.bin";

    const auto missing = client.pullFile("emulator-5556", "/sdcard/nothing.bin", local);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().kind == AdbErrorKind::Rejected);
    const auto folder = client.pullFile("emulator-5556", "/sdcard/DCIM", local);
    REQUIRE_FALSE(folder);
    CHECK(folder.error().kind == AdbErrorKind::Rejected);
    CHECK_FALSE(std::filesystem::exists(local));
    CHECK_FALSE(std::filesystem::exists(partialOf(local)));
}

TEST_CASE("adb client: a file is pushed in 64 KiB chunks with its own mode and modification time")
{
    FakeStorage storage;
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    const AdbClient client(server.port());
    const TempFolder temp;
    const std::string content = pattern(150000);
    const std::filesystem::path local = temp.path() / "photo.jpg";
    writeFile(local, content);
    std::filesystem::last_write_time(local, std::chrono::clock_cast<std::chrono::file_clock>(
                                                std::chrono::sys_seconds(std::chrono::seconds(1700000000))));

    std::vector<std::uint64_t> done;
    REQUIRE(client.pushFile("emulator-5556", local, "/sdcard/Download/photo.jpg",
                            [&done](std::uint64_t sent, std::uint64_t total) {
                                CHECK(total == 150000);
                                done.push_back(sent);
                            }));
    CHECK(done == std::vector<std::uint64_t>{0, 65536, 131072, 150000});
    CHECK(storage.sentChunks() == std::vector<std::uint32_t>{65536, 65536, 18928});
    CHECK(storage.sentTarget() == "/sdcard/Download/photo.jpg,33188");
    CHECK(storage.sentTime() == 1700000000);
    const auto stored = storage.node("/sdcard/Download/photo.jpg");
    REQUIRE(stored.has_value());
    CHECK(stored->content == content);

    std::filesystem::permissions(local,
                                 std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                     std::filesystem::perms::others_write,
                                 std::filesystem::perm_options::remove);
    REQUIRE(client.pushFile("emulator-5556", local, "/sdcard/Download/locked.jpg"));
    CHECK(storage.sentTarget() == "/sdcard/Download/locked.jpg,33060");
    std::filesystem::permissions(local, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);

    const std::filesystem::path empty = temp.path() / "empty.txt";
    writeFile(empty, {});
    REQUIRE(client.pushFile("emulator-5556", empty, "/sdcard/empty.txt"));
    CHECK(storage.sentChunks().empty());
    CHECK(storage.node("/sdcard/empty.txt").has_value());

    const auto gone = client.pushFile("emulator-5556", temp.path() / "gone.txt", "/sdcard/gone.txt");
    REQUIRE_FALSE(gone);
    CHECK(gone.error().kind == AdbErrorKind::Io);
}

TEST_CASE("adb client: a stop request ends a pull at once and leaves no partial file")
{
    FakeStorage storage;
    storage.addFile("/sdcard/big.bin", pattern(300000));
    storage.setStall(true);
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    const TempFolder temp;
    const std::filesystem::path local = temp.path() / "big.bin";
    std::stop_source stop;
    std::promise<void> started;
    std::atomic<bool> signalled{false};
    auto pulling = std::async(std::launch::async, [&] {
        return AdbClient(server.port())
            .pullFile(
                "emulator-5556", "/sdcard/big.bin", local,
                [&](std::uint64_t done, std::uint64_t) {
                    if (done > 0 && !signalled.exchange(true)) {
                        started.set_value();
                    }
                },
                stop.get_token());
    });
    REQUIRE(started.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto start = std::chrono::steady_clock::now();
    stop.request_stop();
    REQUIRE(pulling.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto result = pulling.get();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    REQUIRE_FALSE(result);
    CHECK(result.error().message == "cancelled");
    CHECK_FALSE(std::filesystem::exists(local));
    CHECK_FALSE(std::filesystem::exists(partialOf(local)));
}

TEST_CASE("adb client: a stop request ends a push and the phone keeps nothing")
{
    FakeStorage storage;
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    const TempFolder temp;
    const std::filesystem::path local = temp.path() / "big.bin";
    writeFile(local, pattern(300000));
    std::stop_source stop;
    const auto result = AdbClient(server.port())
                            .pushFile(
                                "emulator-5556", local, "/sdcard/big.bin",
                                [&stop](std::uint64_t done, std::uint64_t) {
                                    if (done >= 65536) {
                                        stop.request_stop();
                                    }
                                },
                                stop.get_token());
    REQUIRE_FALSE(result);
    CHECK(result.error().message == "cancelled");
    REQUIRE(server.waitForHandled(1));
    CHECK_FALSE(storage.exists("/sdcard/big.bin"));
}

TEST_CASE("adb client: names with spaces, quotes and Chinese characters are quoted for the shell")
{
    const std::vector<std::string> paths{"/sdcard/My Photos", "/sdcard/it's mine.txt", "/sdcard/照片 2026"};
    CHECK(AdbClient::removeCommand(paths) ==
          "rm -rf '/sdcard/My Photos' '/sdcard/it'\\''s mine.txt' '/sdcard/照片 2026'");
    CHECK(AdbClient::makeFoldersCommand(std::vector<std::string>{"/sdcard/新 建"}) == "mkdir -p '/sdcard/新 建'");
    CHECK(AdbClient::renameCommand("/sdcard/a b", "/sdcard/it's") ==
          "if [ -e '/sdcard/it'\\''s' ]; then echo 'already exists' >&2; exit 1; fi; mv '/sdcard/a b' '/sdcard/it'\\''s'");

    FakeStorage storage;
    storage.addFile("/sdcard/My Photos/a.jpg", "a");
    storage.addFile("/sdcard/it's mine.txt", "b");
    storage.addFile("/sdcard/taken.txt", "c");
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    const AdbClient client(server.port());

    const std::vector<std::string> folders{"/sdcard/照片 2026/新建 文件夹"};
    REQUIRE(client.makeFolders("emulator-5556", folders));
    CHECK(storage.commands().back() == AdbClient::makeFoldersCommand(folders));
    CHECK(storage.node("/sdcard/照片 2026/新建 文件夹").value_or(FakeStorage::Node{}).directory);

    REQUIRE(client.renamePath("emulator-5556", "/sdcard/My Photos", "/sdcard/它's photos"));
    CHECK(storage.exists("/sdcard/它's photos/a.jpg"));
    CHECK_FALSE(storage.exists("/sdcard/My Photos"));

    const auto taken = client.renamePath("emulator-5556", "/sdcard/it's mine.txt", "/sdcard/taken.txt");
    REQUIRE_FALSE(taken);
    CHECK(taken.error().kind == AdbErrorKind::Rejected);
    CHECK(taken.error().message == "already exists");
    CHECK(storage.exists("/sdcard/it's mine.txt"));

    REQUIRE(client.removePaths("emulator-5556",
                               std::vector<std::string>{"/sdcard/它's photos", "/sdcard/it's mine.txt", "/sdcard/照片 2026"}));
    CHECK_FALSE(storage.exists("/sdcard/它's photos/a.jpg"));
    CHECK_FALSE(storage.exists("/sdcard/it's mine.txt"));
    CHECK_FALSE(storage.exists("/sdcard/照片 2026/新建 文件夹"));
    CHECK(storage.exists("/sdcard/taken.txt"));

    CHECK(client.removePaths("emulator-5556", std::vector<std::string>{"/"}).error().kind == AdbErrorKind::Protocol);
    CHECK(client.removePaths("emulator-5556", std::vector<std::string>{"sdcard/x"}).error().kind ==
          AdbErrorKind::Protocol);
    CHECK(client.renamePath("emulator-5556", "/sdcard/taken.txt", "").error().kind == AdbErrorKind::Protocol);
    CHECK(client.removePaths("emulator-5556", std::vector<std::string>{}));
}
