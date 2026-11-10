#include "gs/adb/WirelessServices.h"

#include <doctest/doctest.h>

using namespace gs::adb;
using Kind = WirelessService::Kind;

TEST_CASE("wireless services as sent by host:mdns:services")
{
    const auto services = parseWirelessServices("adb-FAKE0001-AbCdEf\t_adb-tls-pairing._tcp\t192.168.31.68:37123\n"
                                                "adb-FAKE0002-XyZ\t_adb-tls-connect._tcp\t192.168.31.68:41234\n");
    REQUIRE(services.size() == 2);
    CHECK(services[0] == WirelessService{"adb-FAKE0001-AbCdEf", Kind::Pairing, "192.168.31.68:37123"});
    CHECK(services[1] == WirelessService{"adb-FAKE0002-XyZ", Kind::Connect, "192.168.31.68:41234"});
}

TEST_CASE("wireless services: older servers end the type with a dot; names may have spaces")
{
    const auto services = parseWirelessServices("Pixel 7\t_adb-tls-connect._tcp.\t192.168.1.5:41234\r\n");
    REQUIRE(services.size() == 1);
    CHECK(services[0] == WirelessService{"Pixel 7", Kind::Connect, "192.168.1.5:41234"});
}

TEST_CASE("wireless services: other types and malformed lines are skipped")
{
    CHECK(parseWirelessServices("").empty());
    CHECK(parseWirelessServices("\n\n").empty());
    CHECK(parseWirelessServices("adb-X\t_adb._tcp\t192.168.1.5:5555\n").empty());
    CHECK(parseWirelessServices("adb-X\t_adb-tls-pairing._tcp\n").empty());
    CHECK(parseWirelessServices("adb-X\t_adb-tls-pairing._tcp\t\n").empty());
    CHECK(parseWirelessServices("\t_adb-tls-pairing._tcp\t192.168.1.5:37123\n").empty());
    CHECK(parseWirelessServices("adb-X\t_adb-tls-pairing._tcp\t192.168.1.5:37123\textra\n").empty());
    const auto services = parseWirelessServices("junk\nadb-Y\t_adb-tls-pairing._tcp\t10.0.0.2:37000");
    REQUIRE(services.size() == 1);
    CHECK(services[0] == WirelessService{"adb-Y", Kind::Pairing, "10.0.0.2:37000"});
}
