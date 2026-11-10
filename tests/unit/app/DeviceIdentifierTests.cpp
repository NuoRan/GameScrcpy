#include "DeviceIdentifier.h"

#include "unit/adb/FakeAdbServer.h"

#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

using namespace std::chrono_literals;
using gs::adb::AdbClient;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;
using gs::app::DeviceIdentifier;

namespace {

std::string shellPacket(char id, std::string_view data)
{
    std::string packet(1, id);
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) {
        packet.push_back(static_cast<char>(size >> (8 * i)));
    }
    return packet + std::string(data);
}

void answer(FakeConnection& c)
{
    const std::string transport = c.readRequest();
    const std::string serial = transport.substr(std::string_view("host:transport:").size());
    if (serial != "emulator-5554" && serial != "R58M123ABC") {
        c.write("FAIL" + FakeConnection::prefixed("device '" + serial + "' not found"));
        return;
    }
    c.write("OKAY");
    CHECK(c.readRequest().starts_with("shell,v2,raw:echo \"serialno="));
    const std::string out = serial == "emulator-5554"
                                ? "serialno=EMULATOR37X1X11X0\nbootserialno=EMULATOR37X1X11X0\n"
                                  "androidid=b99ecec0f163b7bc\nrelease=9\nbooted=1\n"
                                : "serialno=R58M123ABC\nbootserialno=R58M123ABC\nandroidid=ab3f31256ae3d44c\n"
                                  "release=14\nbooted=1\n";
    c.write("OKAY" + shellPacket(1, out) + shellPacket(3, std::string(1, '\0')));
}

}

TEST_CASE("device identifier reads each device's ID and version on its own thread")
{
    const FakeAdbServer server(answer);
    std::mutex mutex;
    std::condition_variable done;
    std::map<std::string, std::string> ids;
    DeviceIdentifier identifier(AdbClient(server.port()), [&](const std::string& serial, const auto& info) {
        const std::lock_guard lock(mutex);
        ids[serial] = info ? info->id + " " + info->androidRelease : "error";
        done.notify_all();
    });
    identifier.identify("emulator-5554");
    identifier.identify("R58M123ABC");
    identifier.identify("gone-0001");

    std::unique_lock lock(mutex);
    REQUIRE(done.wait_for(lock, 10s, [&] { return ids.size() == 3; }));
    CHECK(ids.at("emulator-5554") == "aid:b99ecec0f163b7bc 9");
    CHECK(ids.at("R58M123ABC") == "R58M123ABC 14");
    CHECK(ids.at("gone-0001") == "error");
}
