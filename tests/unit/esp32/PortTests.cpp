#include "TestSupport.h"

#include <gs/esp32/Boards.h>
#include <gs/esp32/Port.h>
#include <gs/esp32/UdpPort.h>

#include <doctest/doctest.h>

#include <string>
#include <utility>

using gs::esp32::classifyPort;
using gs::esp32::HostAndPort;
using gs::esp32::KnownBoard;
using gs::esp32::PortKind;

TEST_CASE("serial ports are classified by USB vendor: Espressif's own USB, CH34x and CP210x (HID-004)")
{
    CHECK(classifyPort(0x303A, 0x1001) == PortKind::EspressifUsb);
    CHECK(classifyPort(0x303A, 0x0002) == PortKind::EspressifUsb);
    CHECK(classifyPort(0x1A86, 0x7523) == PortKind::Ch34x);
    CHECK(classifyPort(0x1A86, 0x55D4) == PortKind::Ch34x);
    CHECK(classifyPort(0x10C4, 0xEA60) == PortKind::Cp210x);
    CHECK(classifyPort(0x0403, 0x6001) == PortKind::Other);
    CHECK(classifyPort(0, 0) == PortKind::Other);
    CHECK(gs::esp32::usbSerialJtag(0x303A, 0x1001));
    CHECK_FALSE(gs::esp32::usbSerialJtag(0x303A, 0x0002));
    CHECK_FALSE(gs::esp32::usbSerialJtag(0x1A86, 0x1001));
    CHECK(gs::esp32::probedAutomatically(PortKind::EspressifUsb));
    CHECK(gs::esp32::probedAutomatically(PortKind::Ch34x));
    CHECK(gs::esp32::probedAutomatically(PortKind::Cp210x));
    CHECK_FALSE(gs::esp32::probedAutomatically(PortKind::Other));
    CHECK(gs::esp32::bridge(PortKind::Ch34x));
    CHECK(gs::esp32::bridge(PortKind::Cp210x));
    CHECK_FALSE(gs::esp32::bridge(PortKind::EspressifUsb));
}

TEST_CASE("USB vendor and product come from the device instance id, whatever the bus driver")
{
    using Ids = std::optional<std::pair<std::uint16_t, std::uint16_t>>;
    CHECK(gs::esp32::usbIdsOf("USB\\VID_1A86&PID_7523\\5&2A6E3E2&0&1") == Ids{{0x1A86, 0x7523}});
    CHECK(gs::esp32::usbIdsOf("usb\\vid_303a&pid_1001&mi_00\\6&1234&0&0000") == Ids{{0x303A, 0x1001}});
    CHECK(gs::esp32::usbIdsOf("FTDIBUS\\VID_0403+PID_6001+A50285BIA\\0000") == Ids{{0x0403, 0x6001}});
    CHECK(gs::esp32::usbIdsOf("USB\\VID_10C4&PID_EA60\\0001") == Ids{{0x10C4, 0xEA60}});
    CHECK_FALSE(gs::esp32::usbIdsOf("ACPI\\PNP0501\\1").has_value());
    CHECK_FALSE(gs::esp32::usbIdsOf("USB\\VID_12G4&PID_0001\\1").has_value());
}

TEST_CASE("only COM1 to COM256 are port names")
{
    CHECK(gs::esp32::portNumber("COM1") == 1);
    CHECK(gs::esp32::portNumber("com12") == 12);
    CHECK(gs::esp32::portNumber("COM256") == 256);
    CHECK(gs::esp32::portNumber("COM257") == 0);
    CHECK(gs::esp32::portNumber("COM0") == 0);
    CHECK(gs::esp32::portNumber("COM01") == 0);
    CHECK(gs::esp32::portNumber("LPT1") == 0);
    CHECK(gs::esp32::portNumber("COM") == 0);
    CHECK(gs::esp32::portNumber("auto") == 0);
    CHECK(gs::esp32::validPortName("COM7"));
    CHECK_FALSE(gs::esp32::validPortName("\\\\.\\COM7"));
}

TEST_CASE("a Wi-Fi address is a host with an optional port, 4545 by default")
{
    CHECK(gs::esp32::parseAddress("192.168.1.5", 4545) == HostAndPort{"192.168.1.5", 4545});
    CHECK(gs::esp32::parseAddress(" 192.168.1.5:5000 ", 4545) == HostAndPort{"192.168.1.5", 5000});
    CHECK(gs::esp32::parseAddress("gs-esp32.local", 4545) == HostAndPort{"gs-esp32.local", 4545});
    CHECK(gs::esp32::parseAddress("127.0.0.1", 4545) == HostAndPort{"127.0.0.1", 4545});
    CHECK_FALSE(gs::esp32::parseAddress("", 4545).has_value());
    CHECK_FALSE(gs::esp32::parseAddress("10.0.0.1:0", 4545).has_value());
    CHECK_FALSE(gs::esp32::parseAddress("10.0.0.1:70000", 4545).has_value());
    CHECK_FALSE(gs::esp32::parseAddress("10.0.0.1:port", 4545).has_value());
    CHECK_FALSE(gs::esp32::parseAddress("bad host", 4545).has_value());
    CHECK_FALSE(gs::esp32::parseAddress(":4545", 4545).has_value());
}

TEST_CASE("Wi-Fi keys are remembered per board in esp32-boards.json, and damaged entries are skipped")
{
    const std::array<std::uint8_t, 6> mac{0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    CHECK(gs::esp32::boardId(mac) == "a1b2c3d4e5f6");
    CHECK(gs::esp32::validBoardId("a1b2c3d4e5f6"));
    CHECK_FALSE(gs::esp32::validBoardId("A1B2C3D4E5F6"));
    CHECK_FALSE(gs::esp32::validBoardId("a1b2c3"));

    KnownBoard board;
    board.id = "a1b2c3d4e5f6";
    board.name = "Desk box";
    board.key = gs::esp32::newBoardKey();
    board.address = "192.168.1.20";
    const std::string text = gs::esp32::writeBoards({board});
    CHECK(gs::esp32::parseBoards(text) == std::vector<KnownBoard>{board});
    CHECK(gs::esp32::newBoardKey() != board.key);

    const std::string damaged = R"({"format": "gamescrcpy.esp32-boards", "version": 1, "boards": {
        "a1b2c3d4e5f6": {"key": "00"},
        "NOTANID": {"key": "0000000000000000000000000000000000000000000000000000000000000000"},
        "001122334455": {"name": "Ok", "key": "0101010101010101010101010101010101010101010101010101010101010101"}}})";
    const std::vector<KnownBoard> parsed = gs::esp32::parseBoards(damaged);
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].id == "001122334455");
    CHECK(parsed[0].name == "Ok");
    CHECK(parsed[0].key[31] == 1);
    CHECK(gs::esp32::parseBoards(R"({"format": "other", "version": 1, "boards": {}})").empty());
    CHECK(gs::esp32::parseBoards("not json").empty());

    const esp32test::TemporaryFolder folder;
    const auto file = folder.path() / "esp32-boards.json";
    CHECK(gs::esp32::loadBoards(file).empty());
    REQUIRE(gs::esp32::rememberBoard(file, board));
    KnownBoard other = board;
    other.id = "001122334455";
    other.name = "Second";
    REQUIRE(gs::esp32::rememberBoard(file, other));
    board.name = "Renamed";
    REQUIRE(gs::esp32::rememberBoard(file, board));
    const std::vector<KnownBoard> loaded = gs::esp32::loadBoards(file);
    REQUIRE(loaded.size() == 2);
    CHECK(gs::esp32::findBoard(loaded, "a1b2c3d4e5f6")->name == "Renamed");
    CHECK(gs::esp32::forgetBoard(file, "001122334455"));
    CHECK_FALSE(gs::esp32::forgetBoard(file, "001122334455"));
    CHECK(gs::esp32::loadBoards(file).size() == 1);
    CHECK_FALSE(gs::esp32::findBoard(loaded, "ffffffffffff").has_value());
}
