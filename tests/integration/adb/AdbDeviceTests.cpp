// Runs gs_adb against the real adb server and a connected device or emulator.
//
//   GS_ADB_TEST_SERIAL         device to test (required; the program reports "skipped" without it)
//   GS_ADB_TEST_SECOND_SERIAL  the same device over a second transport, for example its Wi-Fi
//                              address; emulators get one automatically through 127.0.0.1
//   GS_ADB_TEST_OTHER_SERIAL   a different device, whose identity must differ
//   GS_ADB_TEST_RESTART=1      also kill the adb server and let ensureServer() start it again
//   GS_ADB_EXECUTABLE          adb.exe for that restart (default %ANDROID_HOME%\platform-tools)

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "gs/adb/AdbClient.h"
#include "gs/adb/DeviceIdentity.h"
#include "gs/adb/DeviceTracker.h"
#include "gs/adb/ServerLauncher.h"
#include "gs/platform/Crypto.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace gs::adb;
using Clock = std::chrono::steady_clock;

namespace {

std::string g_serial;
std::string g_secondSerial;
std::string g_otherSerial;
std::filesystem::path g_adbExecutable;
bool g_allowRestart = false;

const std::filesystem::path kMissingAdb = "Z:/nonexistent/adb.exe";

std::string environment(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, name);
    std::string text = value != nullptr ? value : "";
    std::free(value);
    return text;
}

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string trimmed(std::string_view text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

std::vector<std::string> lines(std::string_view text)
{
    std::vector<std::string> result;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        result.push_back(trimmed(text.substr(0, newline)));
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
    }
    return result;
}

DWORD listenerProcess(std::uint16_t port)
{
    ULONG size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    std::vector<std::uint8_t> buffer(size);
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) {
        return 0;
    }
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port) {
            return table->table[i].dwOwningPid;
        }
    }
    return 0;
}

int childProcessCount()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int count = 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID == GetCurrentProcessId()) {
            ++count;
        }
    }
    CloseHandle(snapshot);
    return count;
}

std::string sha256Hex(std::span<const std::uint8_t> data)
{
    return gs::platform::toHex(gs::platform::sha256(data));
}

class Socket
{
public:
    Socket() : m_socket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {}
    explicit Socket(SOCKET s) : m_socket(s) {}
    ~Socket() { closesocket(m_socket); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool connectTo(std::uint16_t port)
    {
        sockaddr_in address = loopback(port);
        return connect(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }

    std::uint16_t listenOnAnyPort()
    {
        sockaddr_in address = loopback(0);
        bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        listen(m_socket, 1);
        int size = sizeof(address);
        getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &size);
        return ntohs(address.sin_port);
    }

    SOCKET acceptOne(std::chrono::milliseconds timeout)
    {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(m_socket, &ready);
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        timeval limit{static_cast<long>(seconds.count()),
                      static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds).count())};
        if (select(0, &ready, nullptr, nullptr, &limit) != 1) {
            return INVALID_SOCKET;
        }
        return accept(m_socket, nullptr, nullptr);
    }

    void sendText(std::string_view text) { send(m_socket, text.data(), static_cast<int>(text.size()), 0); }
    void finishSending() { shutdown(m_socket, SD_SEND); }

    std::string receiveAll(std::chrono::milliseconds timeout = std::chrono::seconds(10))
    {
        setReadTimeout(timeout);
        std::string data;
        char buffer[4096];
        for (int n; (n = recv(m_socket, buffer, sizeof(buffer), 0)) > 0;) {
            data.append(buffer, static_cast<std::size_t>(n));
        }
        return data;
    }

    std::string receiveLine(std::chrono::milliseconds timeout = std::chrono::seconds(10))
    {
        setReadTimeout(timeout);
        std::string data;
        char c = 0;
        while (recv(m_socket, &c, 1, 0) == 1) {
            data.push_back(c);
            if (c == '\n') {
                break;
            }
        }
        return data;
    }

private:
    void setReadTimeout(std::chrono::milliseconds timeout)
    {
        const auto ms = static_cast<DWORD>(timeout.count());
        setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    }

public:

private:
    static sockaddr_in loopback(std::uint16_t port)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return address;
    }

    SOCKET m_socket;
};

class TrackerLog
{
public:
    DeviceTracker::Callback callback()
    {
        return [this](const TrackerUpdate& update) {
            {
                const std::lock_guard lock(m_mutex);
                m_updates.push_back(update);
            }
            m_changed.notify_all();
        };
    }

    bool waitFor(const std::function<bool(const TrackerUpdate&)>& condition, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return !m_updates.empty() && condition(m_updates.back()); });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<TrackerUpdate> m_updates;
};

bool hasOnlineDevice(const TrackerUpdate& update, std::string_view serial)
{
    for (const DeviceEntry& device : update.devices) {
        if (device.serial == serial && device.state == DeviceState::Device) {
            return true;
        }
    }
    return false;
}

std::string deviceIdentity(const AdbClient& client, std::string_view serial)
{
    const auto info = readDeviceInfo(client, serial);
    REQUIRE(info);
    return info->id;
}

}

TEST_CASE("R3: the running adb server is used as it is")
{
    const AdbClient client;
    const DWORD before = listenerProcess(AdbClient::kDefaultPort);
    REQUIRE(before != 0);
    const auto version = ensureServer(client, kMissingAdb);
    REQUIRE(version);
    MESSAGE("adb server version " << *version << ", process " << before);
    CHECK(*version >= AdbClient::kMinimumServerVersion);
    CHECK(listenerProcess(AdbClient::kDefaultPort) == before);
}

TEST_CASE("the device is online and speaks shell v2")
{
    const AdbClient client;
    const auto devices = client.devices();
    REQUIRE(devices);
    bool online = false;
    for (const DeviceEntry& device : *devices) {
        online = online || (device.serial == g_serial && device.state == DeviceState::Device);
    }
    CHECK(online);
    const auto features = client.features(g_serial);
    REQUIRE(features);
    CHECK(std::find(features->begin(), features->end(), "shell_v2") != features->end());
}

TEST_CASE("ADB-003: shell output and exit status without adb.exe child processes")
{
    const AdbClient client;
    CHECK(childProcessCount() == 0);

    const auto mixed = client.shell(g_serial, "echo out; echo err 1>&2; exit 3");
    REQUIRE(mixed);
    CHECK(mixed->out == "out\n");
    CHECK(mixed->err == "err\n");
    CHECK(mixed->exitCode == 3);

    const auto unicode = client.shell(g_serial, "echo " + shellQuote("中文 ✓"));
    REQUIRE(unicode);
    CHECK(unicode->out == "中文 ✓\n");

    const auto start = Clock::now();
    const auto large = client.shell(g_serial, "seq 1 200000");
    const double largeMs = millisecondsSince(start);
    REQUIRE(large);
    const std::vector<std::string> numbers = lines(large->out);
    CHECK(numbers.size() == 200000);
    CHECK(numbers.back() == "200000");
    MESSAGE(std::format("seq 1 200000: {} bytes in {:.1f} ms", large->out.size(), largeMs));

    constexpr int kRounds = 20;
    const auto roundsStart = Clock::now();
    for (int i = 0; i < kRounds; ++i) {
        REQUIRE(client.shell(g_serial, "true"));
    }
    MESSAGE(std::format("shell round trip: {:.1f} ms average", millisecondsSince(roundsStart) / kRounds));

    CHECK(childProcessCount() == 0);
}

TEST_CASE("ADB-004: push verified by size and a device-side checksum")
{
    const AdbClient client;
    std::vector<std::uint8_t> content((1U << 20U) + 12345U);
    std::mt19937 random(20261001);
    for (std::uint8_t& byte : content) {
        byte = static_cast<std::uint8_t>(random());
    }
    const std::string remote = std::format("/data/local/tmp/gs_adb_it_{}.bin", GetCurrentProcessId());

    const auto start = Clock::now();
    REQUIRE(client.push(g_serial, content, remote, 0644));
    const double pushMs = millisecondsSince(start);
    MESSAGE(std::format("pushed {} bytes in {:.1f} ms ({:.1f} MB/s)", content.size(), pushMs,
                        static_cast<double>(content.size()) / 1e3 / pushMs));

    const auto info = client.stat(g_serial, remote);
    REQUIRE(info);
    REQUIRE(info->has_value());
    CHECK(((*info)->mode & 0170000U) == 0100000U);
    CHECK(((*info)->mode & 0777U) == 0666U);
    CHECK((*info)->size == content.size());

    const auto sum = client.shell(g_serial, "sha256sum " + shellQuote(remote));
    REQUIRE(sum);
    CHECK(sum->out.substr(0, 64) == sha256Hex(content));

    REQUIRE(client.shell(g_serial, "rm -f " + shellQuote(remote)));
    const auto gone = client.stat(g_serial, remote);
    REQUIRE(gone);
    CHECK_FALSE(gone->has_value());
}

TEST_CASE("ADB-005: forward reaches a device listener and can be removed")
{
    const AdbClient client;
    auto listener = client.spawn(g_serial, "toybox nc -s 127.0.0.1 -l cat");
    REQUIRE(listener);
    const auto announced = listener->read();
    REQUIRE(announced);
    REQUIRE(announced->id == static_cast<std::uint8_t>(ShellPacketId::Stdout));
    const std::string devicePort = trimmed(std::string(announced->data.begin(), announced->data.end()));

    const auto localPort = client.forward(g_serial, "tcp:" + devicePort);
    REQUIRE(localPort);
    {
        Socket socket;
        REQUIRE(socket.connectTo(*localPort));
        socket.sendText("ping-forward\n");
        CHECK(socket.receiveLine() == "ping-forward\n");
    }
    const ForwardEntry entry{g_serial, std::format("tcp:{}", *localPort), "tcp:" + devicePort};
    auto forwards = client.forwards();
    REQUIRE(forwards);
    CHECK(std::find(forwards->begin(), forwards->end(), entry) != forwards->end());

    REQUIRE(client.removeForward(g_serial, *localPort));
    forwards = client.forwards();
    REQUIRE(forwards);
    CHECK(std::find(forwards->begin(), forwards->end(), entry) == forwards->end());
    CHECK(client.removeForward(g_serial, *localPort).error().kind == AdbErrorKind::Rejected);
}

TEST_CASE("ADB-005: reverse reaches a host listener and can be removed")
{
    const AdbClient client;
    Socket listener;
    const std::uint16_t hostPort = listener.listenOnAnyPort();
    const std::string device = std::format("tcp:{}", 28000 + GetCurrentProcessId() % 1000);

    REQUIRE(client.reverse(g_serial, device, hostPort));
    const ForwardEntry entry{"", std::format("tcp:{}", hostPort), device};
    const auto matches = [&](const ForwardEntry& e) { return e.host == entry.host && e.device == entry.device; };
    auto reverses = client.reverses(g_serial);
    REQUIRE(reverses);
    CHECK(std::any_of(reverses->begin(), reverses->end(), matches));

    auto peer = client.spawn(g_serial, "toybox nc 127.0.0.1 " + device.substr(4));
    REQUIRE(peer);
    {
        const SOCKET accepted = listener.acceptOne(std::chrono::seconds(10));
        REQUIRE(accepted != INVALID_SOCKET);
        Socket connection(accepted);
        const std::string line = "reverse-ok\n";
        REQUIRE(peer->writeStdin(std::span(reinterpret_cast<const std::uint8_t*>(line.data()), line.size())));
        CHECK(connection.receiveLine() == line);
        connection.sendText("ack\n");
        std::string answer;
        while (answer.find('\n') == std::string::npos) {
            const auto packet = peer->read();
            REQUIRE(packet);
            if (packet->id == static_cast<std::uint8_t>(ShellPacketId::Stdout)) {
                answer.append(packet->data.begin(), packet->data.end());
            }
        }
        CHECK(answer == "ack\n");
    }

    REQUIRE(client.removeReverse(g_serial, device));
    reverses = client.reverses(g_serial);
    REQUIRE(reverses);
    CHECK_FALSE(std::any_of(reverses->begin(), reverses->end(), matches));
}

TEST_CASE("spawn: stdin reaches the device process")
{
    auto process = AdbClient().spawn(g_serial, "cat");
    REQUIRE(process);
    const std::string token = "c2VjcmV0LXRva2Vu\n";
    REQUIRE(process->writeStdin(std::span(reinterpret_cast<const std::uint8_t*>(token.data()), token.size())));
    REQUIRE(process->closeStdin());
    std::string out;
    std::optional<int> exitCode;
    while (auto packet = process->read()) {
        if (packet->id == static_cast<std::uint8_t>(ShellPacketId::Stdout)) {
            out.append(packet->data.begin(), packet->data.end());
        } else if (packet->id == static_cast<std::uint8_t>(ShellPacketId::Exit)) {
            exitCode = packet->data.empty() ? -1 : packet->data.front();
        }
    }
    CHECK(out == token);
    CHECK(exitCode == 0);
}

TEST_CASE("ADB-006 / R4: one device over two transports has one identity")
{
    const AdbClient client;
    const std::string id = deviceIdentity(client, g_serial);
    MESSAGE("stable id of " << g_serial << ": " << id);
    CHECK_FALSE(id.empty());
    if (!g_otherSerial.empty()) {
        const std::string other = deviceIdentity(client, g_otherSerial);
        MESSAGE("stable id of " << g_otherSerial << ": " << other);
        CHECK(other != id);
    }

    std::string second = g_secondSerial;
    const bool emulator = second.empty() && g_serial.starts_with("emulator-");
    if (emulator) {
        second = std::format("127.0.0.1:{}", std::stoi(g_serial.substr(9)) + 1);
        const auto connected = client.connect(second);
        REQUIRE(connected);
        MESSAGE(*connected);
    }
    if (second.empty()) {
        MESSAGE("no second transport; set GS_ADB_TEST_SECOND_SERIAL");
        return;
    }
    {
        TrackerLog log;
        const DeviceTracker tracker(AdbClient::kDefaultPort, log.callback());
        REQUIRE(log.waitFor([&](const TrackerUpdate& u) { return hasOnlineDevice(u, second); },
                            std::chrono::seconds(10)));
    }
    CHECK(deviceIdentity(client, second) == id);
    if (emulator) {
        CHECK(client.disconnect(second));
    }
}

TEST_CASE("ADB-007: pairing or connecting where nothing listens fails with adb's reason")
{
    const AdbClient client;
    const auto paired = client.pair("127.0.0.1:1", "123456");
    REQUIRE_FALSE(paired);
    MESSAGE(paired.error().message);
    CHECK(paired.error().kind == AdbErrorKind::Rejected);
    CHECK(paired.error().message.starts_with("Failed"));

    const auto connected = client.connect("127.0.0.1:1");
    REQUIRE_FALSE(connected);
    CHECK(connected.error().kind == AdbErrorKind::Rejected);
    CHECK(connected.error().message.starts_with("cannot connect to 127.0.0.1:1"));
}

TEST_CASE("ADB-007: the adb server lists the wireless services it discovers")
{
    const auto services = AdbClient().wirelessServices();
    REQUIRE(services);
    for (const WirelessService& service : *services) {
        MESSAGE(std::format("{} {} at {}", service.kind == WirelessService::Kind::Pairing ? "pairing" : "connect",
                            service.name, service.address));
    }
}

TEST_CASE("ADB-001: a stopped server is started again and the device comes back")
{
    if (!g_allowRestart) {
        MESSAGE("skipped; set GS_ADB_TEST_RESTART=1 to kill and restart the adb server");
        return;
    }
    REQUIRE(std::filesystem::exists(g_adbExecutable));
    const AdbClient client;
    const DWORD oldServer = listenerProcess(AdbClient::kDefaultPort);

    TrackerLog log;
    const DeviceTracker tracker(AdbClient::kDefaultPort, log.callback());
    REQUIRE(log.waitFor([](const TrackerUpdate& u) { return hasOnlineDevice(u, g_serial); }, std::chrono::seconds(5)));

    {
        Socket kill;
        REQUIRE(kill.connectTo(AdbClient::kDefaultPort));
        kill.sendText("0009host:kill");
        (void)kill.receiveAll();
    }
    REQUIRE(log.waitFor([](const TrackerUpdate& u) { return !u.serverAvailable; }, std::chrono::seconds(5)));
    CHECK(client.serverVersion().error().kind == AdbErrorKind::ServerUnavailable);

    const auto start = Clock::now();
    const auto version = ensureServer(client, g_adbExecutable);
    REQUIRE(version);
    const double startedMs = millisecondsSince(start);
    REQUIRE(log.waitFor([](const TrackerUpdate& u) { return hasOnlineDevice(u, g_serial); }, std::chrono::seconds(30)));
    MESSAGE(std::format("server {} started in {:.0f} ms; {} online again after {:.0f} ms", *version, startedMs,
                        g_serial, millisecondsSince(start)));
    CHECK(listenerProcess(AdbClient::kDefaultPort) != oldServer);
}

int main(int argc, char** argv)
{
    g_serial = environment("GS_ADB_TEST_SERIAL");
    if (g_serial.empty()) {
        std::printf("gs_adb_device_tests: skipped, set GS_ADB_TEST_SERIAL to a connected device\n");
        return 77;
    }
    g_secondSerial = environment("GS_ADB_TEST_SECOND_SERIAL");
    g_otherSerial = environment("GS_ADB_TEST_OTHER_SERIAL");
    g_allowRestart = environment("GS_ADB_TEST_RESTART") == "1";
    g_adbExecutable = environment("GS_ADB_EXECUTABLE");
    if (g_adbExecutable.empty()) {
        g_adbExecutable = std::filesystem::path(environment("ANDROID_HOME")) / "platform-tools" / "adb.exe";
    }
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);

    doctest::Context context(argc, argv);
    const int result = context.run();
    WSACleanup();
    return result;
}
