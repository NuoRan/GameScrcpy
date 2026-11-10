#include "fakes/RecordingListener.h"
#include "unit/adb/FakeAdbServer.h"

#include "gs/platform/Crypto.h"
#include "gs/session/AdbServerHost.h"
#include "gs/session/Session.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace gs::session::test;
using gs::adb::test::closedPort;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;

namespace {

constexpr std::string_view kSerial = "emulator-5556";

std::string le32(std::uint32_t value)
{
    std::string bytes(4, '\0');
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<char>((value >> (8 * i)) & 0xFFU);
    }
    return bytes;
}

std::string shellPacket(char id, std::string_view data)
{
    return std::string(1, id) + le32(static_cast<std::uint32_t>(data.size())) + std::string(data);
}

std::shared_ptr<const ServerPackage> package()
{
    return std::make_shared<const ServerPackage>(std::vector<std::uint8_t>{'a', 'b', 'c'});
}

FakeAdbServer::Handler device(std::string_view listLine, std::string_view sdk = "35")
{
    return [listLine = std::string(listLine), sdk = std::string(sdk)](FakeConnection& c) {
        const std::string request = c.readRequest();
        if (request == "host:devices-l") {
            c.write("OKAY" + FakeConnection::prefixed(listLine + "\n"));
            return;
        }
        if (request == "host:transport:" + std::string(kSerial)) {
            c.write("OKAY");
            const std::string service = c.readRequest();
            CHECK(service == "shell,v2,raw:getprop ro.build.version.sdk");
            c.write("OKAY" + shellPacket(1, sdk + "\n") + shellPacket(3, std::string(1, '\0')));
            return;
        }
        FAIL_CHECK("unexpected adb request: " << request);
    };
}

SessionFailure runUntilFailed(adb::AdbClient client, std::shared_ptr<const ServerPackage> server = package())
{
    RecordingListener listener;
    Session session(std::make_unique<AdbServerHost>(client, std::string(kSerial), std::move(server)), {}, listener);
    session.start();
    REQUIRE(listener.waitForEnd());
    REQUIRE(listener.failure());
    return *listener.failure();
}

}

TEST_CASE("the server package is named after its content")
{
    CHECK(package()->remotePath() == "/data/local/tmp/gs-server-ba7816bf8f01cfea.jar");
    CHECK(ServerPackage::load("Z:/nonexistent/server.apk") == nullptr);
}

TEST_CASE("a device that has not allowed USB debugging fails while deploying")
{
    FakeAdbServer adb(device("emulator-5556          unauthorized transport_id:4"));
    const SessionFailure failure = runUntilFailed(adb::AdbClient(adb.port()));
    CHECK(failure.kind == FailureKind::DeviceUnauthorized);
    CHECK(failure.phase == SessionPhase::Deploying);
}

TEST_CASE("a device older than Android 9 is refused before anything is pushed")
{
    FakeAdbServer adb(device("emulator-5556          device product:p model:m device:d transport_id:4", "26"));
    const SessionFailure failure = runUntilFailed(adb::AdbClient(adb.port()));
    CHECK(failure.kind == FailureKind::UnsupportedAndroid);
    CHECK(failure.phase == SessionPhase::Deploying);
    CHECK(failure.detail.find("API 26") != std::string::npos);
}

TEST_CASE("an offline device and an unknown serial are told apart")
{
    {
        FakeAdbServer adb(device("emulator-5556          offline transport_id:4"));
        const SessionFailure failure = runUntilFailed(adb::AdbClient(adb.port()));
        CHECK(failure.kind == FailureKind::DeviceOffline);
        CHECK(failure.detail.find("offline") != std::string::npos);
    }
    {
        FakeAdbServer adb(device("R58M20ABCDE            device transport_id:7"));
        CHECK(runUntilFailed(adb::AdbClient(adb.port())).kind == FailureKind::DeviceNotFound);
    }
}

TEST_CASE("without an adb server, adb is reported unavailable")
{
    const SessionFailure failure = runUntilFailed(adb::AdbClient(closedPort()));
    CHECK(failure.kind == FailureKind::AdbUnavailable);
    CHECK(failure.phase == SessionPhase::Deploying);
}

TEST_CASE("a missing server package fails before adb is asked anything")
{
    FakeAdbServer adb([](FakeConnection& c) { FAIL_CHECK("unexpected adb request: " << c.readRequest()); });
    const SessionFailure failure = runUntilFailed(adb::AdbClient(adb.port()), nullptr);
    CHECK(failure.kind == FailureKind::DeployFailed);
}

TEST_CASE("a device adb loses while it is prepared is waited for and prepared again once it is back (RC1 D-016)")
{
    std::vector<std::string> comeback;
    SUBCASE("over wireless adb, which reconnects the same transport")
    {
        comeback = {"emulator-5556          offline transport_id:4\n", "emulator-5556          device transport_id:4\n"};
    }
    SUBCASE("over USB, where the device is back on a new transport")
    {
        comeback = {"emulator-5556          device transport_id:5\n"};
    }
    std::atomic<int> asked{0};
    std::atomic<int> tracked{0};
    FakeAdbServer adb([&](FakeConnection& c) {
        const std::string request = c.readRequest();
        if (request == "host:devices-l") {
            const std::string line = asked == 0   ? "emulator-5556          device transport_id:4\n"
                                     : asked == 1 ? "emulator-5556          offline transport_id:4\n"
                                                  : comeback.back();
            c.write("OKAY" + FakeConnection::prefixed(line));
            return;
        }
        if (request == "host:track-devices-l") {
            ++tracked;
            c.write("OKAY");
            for (const std::string& list : comeback) {
                c.write(FakeConnection::prefixed(list));
            }
            asked = 2;
            c.waitForClientClose();
            return;
        }
        if (request == "host:transport:" + std::string(kSerial)) {
            c.write("OKAY");
            CHECK(c.readRequest() == "shell,v2,raw:getprop ro.build.version.sdk");
            if (asked == 0) {
                asked = 1;
                c.write("FAIL" + FakeConnection::prefixed("closed"));
                return;
            }
            c.write("OKAY" + shellPacket(1, "26\n") + shellPacket(3, std::string(1, '\0')));
            return;
        }
        FAIL_CHECK("unexpected adb request: " << request);
    });
    const SessionFailure failure = runUntilFailed(adb::AdbClient(adb.port()));
    CHECK(tracked == 1);
    CHECK(failure.kind == FailureKind::UnsupportedAndroid);
    CHECK(failure.phase == SessionPhase::Deploying);
}

TEST_CASE("a failure while preparing a device that stays on adb is reported at once, without waiting for it")
{
    FakeAdbServer adb([](FakeConnection& c) {
        const std::string request = c.readRequest();
        if (request == "host:devices-l") {
            c.write("OKAY" + FakeConnection::prefixed("emulator-5556          device transport_id:4\n"));
            return;
        }
        if (request == "host:transport:" + std::string(kSerial)) {
            c.write("OKAY");
            const std::string service = c.readRequest();
            if (service == "shell,v2,raw:getprop ro.build.version.sdk") {
                c.write("OKAY" + shellPacket(1, "35\n") + shellPacket(3, std::string(1, '\0')));
                return;
            }
            CHECK(service == "sync:");
            c.write("FAIL" + FakeConnection::prefixed("read-only file system"));
            return;
        }
        FAIL_CHECK("unexpected adb request: " << request);
    });
    const auto began = std::chrono::steady_clock::now();
    const SessionFailure failure = runUntilFailed(adb::AdbClient(adb.port()));
    CHECK(failure.kind == FailureKind::DeployFailed);
    CHECK(failure.detail.find("read-only") != std::string::npos);
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(5));
}
