#include "gs/adb/DeviceList.h"

#include <doctest/doctest.h>

using namespace gs::adb;

TEST_CASE("device list: long format as sent by adb server 41")
{
    const auto devices = parseDeviceList("emulator-5556          device product:sdk_gphone64_x86_64 "
                                         "model:sdk_gphone64_x86_64 device:emu64xa transport_id:4\n");
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].serial == "emulator-5556");
    CHECK(devices[0].state == DeviceState::Device);
    CHECK(devices[0].product == "sdk_gphone64_x86_64");
    CHECK(devices[0].model == "sdk_gphone64_x86_64");
    CHECK(devices[0].device == "emu64xa");
    CHECK(devices[0].transportId == 4);
}

TEST_CASE("device list: several devices, states and the short format")
{
    const auto devices = parseDeviceList("R58M123ABC\tunauthorized usb:1-1 transport_id:7\n"
                                         "192.168.1.20:5555      offline transport_id:9\r\n"
                                         "0123456789\tdevice\n"
                                         "adb-R58M-abc._adb-tls-connect._tcp device product:x transport_id:12\n");
    REQUIRE(devices.size() == 4);
    CHECK(devices[0].state == DeviceState::Unauthorized);
    CHECK(devices[0].transportId == 7);
    CHECK(devices[1].serial == "192.168.1.20:5555");
    CHECK(devices[1].state == DeviceState::Offline);
    CHECK(devices[1].transportId == 9);
    CHECK(devices[2].serial == "0123456789");
    CHECK(devices[2].state == DeviceState::Device);
    CHECK(devices[2].transportId == 0);
    CHECK(devices[3].serial == "adb-R58M-abc._adb-tls-connect._tcp");
}

TEST_CASE("device list: multi-word states and malformed lines")
{
    const auto devices = parseDeviceList("\n   \nlonely\n"
                                         "ABC no permissions (missing udev rules); see [http://x] usb:1-2 transport_id:3\n"
                                         "DEF weird-state transport_id:notanumber\n");
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].serial == "ABC");
    CHECK(devices[0].state == DeviceState::NoPermissions);
    CHECK(devices[0].transportId == 3);
    CHECK(devices[1].state == DeviceState::Unknown);
    CHECK(devices[1].transportId == 0);
}

TEST_CASE("device list: empty list")
{
    CHECK(parseDeviceList("").empty());
}

TEST_CASE("device state names")
{
    CHECK(parseDeviceState("device") == DeviceState::Device);
    CHECK(parseDeviceState("authorizing") == DeviceState::Authorizing);
    CHECK(parseDeviceState("connecting") == DeviceState::Connecting);
    CHECK(parseDeviceState("recovery") == DeviceState::Recovery);
    CHECK(parseDeviceState("sideload") == DeviceState::Sideload);
    CHECK(parseDeviceState("bootloader") == DeviceState::Bootloader);
    CHECK(parseDeviceState("host") == DeviceState::Host);
    CHECK(parseDeviceState("Device") == DeviceState::Unknown);
}
