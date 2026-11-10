#include "FakeAdbServer.h"

#include "gs/adb/ServerLauncher.h"

#include <doctest/doctest.h>

using namespace gs::adb;
using gs::adb::test::closedPort;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;

namespace {

const std::filesystem::path kMissingAdb = "Z:/nonexistent/adb.exe";

}

TEST_CASE("ensure server: a running server of another version is used as is (R3)")
{
    std::string version;
    FakeAdbServer server([&](FakeConnection& c) {
        CHECK(c.readRequest() == "host:version");
        c.write("OKAY" + FakeConnection::prefixed(version));
    });
    const AdbClient client(server.port());

    version = "0028";
    auto used = ensureServer(client, kMissingAdb);
    REQUIRE(used);
    CHECK(*used == 40);

    version = "0027";
    used = ensureServer(client, kMissingAdb);
    REQUIRE(used);
    CHECK(*used == AdbClient::kMinimumServerVersion);
}

TEST_CASE("ensure server: a too old server is reported, not replaced")
{
    FakeAdbServer server([](FakeConnection& c) {
        (void)c.readRequest();
        c.write("OKAY" + FakeConnection::prefixed("001f"));
    });
    const auto used = ensureServer(AdbClient(server.port()), kMissingAdb);
    REQUIRE_FALSE(used);
    CHECK(used.error().kind == AdbErrorKind::ServerTooOld);
    REQUIRE(server.waitForHandled(1));
}

TEST_CASE("ensure server: no server and no adb executable")
{
    const auto used = ensureServer(AdbClient(closedPort()), kMissingAdb);
    REQUIRE_FALSE(used);
    CHECK(used.error().kind == AdbErrorKind::LaunchFailed);
}
