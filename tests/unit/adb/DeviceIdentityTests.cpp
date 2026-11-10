#include "FakeAdbServer.h"

#include "gs/adb/DeviceIdentity.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>

using namespace gs::adb;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;

namespace {

constexpr std::string_view kIdentityCommand =
    "shell,v2,raw:echo \"serialno=$(getprop ro.serialno)\"; echo \"bootserialno=$(getprop ro.boot.serialno)\"; "
    "echo \"androidid=$(settings get secure android_id 2>/dev/null)\"; "
    "echo \"release=$(getprop ro.build.version.release)\"; "
    "echo \"manufacturer=$(getprop ro.product.manufacturer)\"; "
    "echo \"model=$(getprop ro.product.model)\"; "
    "echo \"marketname=$(getprop ro.product.marketname)\"; "
    "echo \"booted=$(getprop sys.boot_completed)\"";

std::string shellPacket(char id, std::string_view data)
{
    std::string packet(1, id);
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) {
        packet.push_back(static_cast<char>(size >> (8 * i)));
    }
    return packet + std::string(data);
}

FakeAdbServer deviceAnswering(std::string out)
{
    return FakeAdbServer([out = std::move(out)](FakeConnection& c) {
        CHECK(c.readRequest() == "host:transport:emulator-5556");
        c.write("OKAY");
        CHECK(c.readRequest() == kIdentityCommand);
        c.write("OKAY" + shellPacket(1, out) + shellPacket(3, std::string(1, '\0')));
    });
}

}

TEST_CASE("generic serial numbers")
{
    CHECK(isGenericSerial(""));
    CHECK(isGenericSerial("unknown"));
    CHECK(isGenericSerial("UNKNOWN"));
    CHECK(isGenericSerial("0123456789ABCDEF"));
    CHECK(isGenericSerial("0123456789abcdef"));
    CHECK(isGenericSerial("1234567890ABCDEF"));
    CHECK(isGenericSerial("123456789ABCDEF"));
    CHECK(isGenericSerial("0123456789"));
    CHECK(isGenericSerial("0000000000000000"));
    CHECK(isGenericSerial("0"));
    CHECK(isGenericSerial("aaaaaaaa"));

    CHECK(isGenericSerial("EMULATOR37X1X11X0"));
    CHECK(isGenericSerial("emulator30X4X5X0"));

    CHECK_FALSE(isGenericSerial("R58M123ABC"));
    CHECK_FALSE(isGenericSerial("EMULATO"));
    CHECK_FALSE(isGenericSerial("0123456789ABCDEF0"));
    CHECK_FALSE(isGenericSerial("00000001"));
}

TEST_CASE("stable device id prefers ro.serialno, then ro.boot.serialno, then the Android ID")
{
    CHECK(stableDeviceId("R58M123ABC", "R58M123ABC", "ab3f31256ae3d44c") == "R58M123ABC");
    CHECK(stableDeviceId("R58M123ABC", "", "") == "R58M123ABC");
    CHECK(stableDeviceId("", "R58M123ABC", "ab3f31256ae3d44c") == "R58M123ABC");
    CHECK(stableDeviceId("0123456789ABCDEF", "HW-77AB", "ab3f31256ae3d44c") == "HW-77AB");
    CHECK(stableDeviceId("0123456789ABCDEF", "0123456789ABCDEF", "ab3f31256ae3d44c") == "aid:ab3f31256ae3d44c");
    CHECK(stableDeviceId("", "", "ab3f31256ae3d44c") == "aid:ab3f31256ae3d44c");
}

TEST_CASE("stable device id: two phones sharing a generic serial stay apart")
{
    const std::string a = stableDeviceId("0123456789ABCDEF", "unknown", "1111222233334444");
    const std::string b = stableDeviceId("0123456789ABCDEF", "unknown", "5555666677778888");
    CHECK(a != b);
}

TEST_CASE("stable device id: two emulator instances stay apart")
{
    const std::string api28 = stableDeviceId("EMULATOR37X1X11X0", "EMULATOR37X1X11X0", "b99ecec0f163b7bc");
    const std::string api35 = stableDeviceId("EMULATOR37X1X11X0", "EMULATOR37X1X11X0", "ab3f31256ae3d44c");
    CHECK(api28 == "aid:b99ecec0f163b7bc");
    CHECK(api35 == "aid:ab3f31256ae3d44c");
}

TEST_CASE("stable device id: nothing usable falls back to the generic serial")
{
    CHECK(stableDeviceId("0123456789ABCDEF", "", "") == "0123456789ABCDEF");
    CHECK(stableDeviceId("", "", "") == "");
}

TEST_CASE("device info read from a device: a phone with its serial number")
{
    const FakeAdbServer server = deviceAnswering("serialno=R58M123ABC\nbootserialno=R58M123ABC\n"
                                                 "androidid=ab3f31256ae3d44c\nrelease=14\nmanufacturer=samsung\n"
                                                 "model=SM-S921B\nmarketname=\nbooted=1\n");
    const auto info = readDeviceInfo(AdbClient(server.port()), "emulator-5556");
    REQUIRE(info);
    CHECK(*info == DeviceInfo{"R58M123ABC", "14", "samsung", "SM-S921B", ""});
}

TEST_CASE("device info read from a device: a vendor's market name")
{
    const FakeAdbServer server = deviceAnswering("serialno=8f4c2a91\nbootserialno=8f4c2a91\n"
                                                 "androidid=5d0e6b7a9c3f1e24\nrelease=14\nmanufacturer=Xiaomi\n"
                                                 "model=23113RKC6C\nmarketname=Redmi K70\nbooted=1\n");
    const auto info = readDeviceInfo(AdbClient(server.port()), "emulator-5556");
    REQUIRE(info);
    CHECK(info->model == "23113RKC6C");
    CHECK(info->marketName == "Redmi K70");
}

TEST_CASE("device info read from a device: an emulator goes by its Android ID")
{
    const FakeAdbServer server = deviceAnswering("serialno=EMULATOR37X1X11X0\r\nbootserialno=EMULATOR37X1X11X0\r\n"
                                                 "androidid=ab3f31256ae3d44c\r\nrelease=15\r\nmanufacturer=Google\r\n"
                                                 "model=sdk_gphone64_x86_64\r\nmarketname=\r\nbooted=1\r\n");
    const auto info = readDeviceInfo(AdbClient(server.port()), "emulator-5556");
    REQUIRE(info);
    CHECK(info->id == "aid:ab3f31256ae3d44c");
    CHECK(info->androidRelease == "15");
    CHECK(info->manufacturer == "Google");
}

TEST_CASE("device info read from a device: a booting emulator has no ID yet, but its version")
{
    const FakeAdbServer server = deviceAnswering("serialno=EMULATOR37X1X11X0\nbootserialno=EMULATOR37X1X11X0\n"
                                                 "androidid=\nrelease=15\nmanufacturer=Google\n"
                                                 "model=sdk_gphone64_x86_64\nmarketname=\nbooted=\n");
    const auto info = readDeviceInfo(AdbClient(server.port()), "emulator-5556");
    REQUIRE(info);
    CHECK(info->id.empty());
    CHECK(info->androidRelease == "15");
}

TEST_CASE("device info read from a device: a missing value leaves the others in place")
{
    const FakeAdbServer server = deviceAnswering("serialno=R58M123ABC\nbootserialno=R58M123ABC\n"
                                                 "release=14\nmanufacturer=samsung\nmodel=SM-S921B\nbooted=1\n");
    const auto info = readDeviceInfo(AdbClient(server.port()), "emulator-5556");
    REQUIRE(info);
    CHECK(*info == DeviceInfo{"R58M123ABC", "14", "samsung", "SM-S921B", ""});
}

TEST_CASE("device info read from a device: a booted device with nothing better keeps its generic serial")
{
    const FakeAdbServer server = deviceAnswering("serialno=0123456789ABCDEF\nbootserialno=\nandroidid=null\n"
                                                 "release=\nmanufacturer=\nmodel=\nmarketname=\nbooted=1\n");
    const auto info = readDeviceInfo(AdbClient(server.port()), "emulator-5556");
    REQUIRE(info);
    CHECK(*info == DeviceInfo{"0123456789ABCDEF", "", "", "", ""});
}

TEST_CASE("device info read from a device: an unauthorized device fails")
{
    const FakeAdbServer server([](FakeConnection& c) {
        CHECK(c.readRequest() == "host:transport:emulator-5556");
        c.write("FAIL" + FakeConnection::prefixed("device unauthorized."));
    });
    const auto info = readDeviceInfo(AdbClient(server.port()), "emulator-5556");
    REQUIRE_FALSE(info);
    CHECK(info.error().message == "device unauthorized.");
}
