#include "gs/adb/ForwardList.h"

#include <doctest/doctest.h>

using namespace gs::adb;

TEST_CASE("forward list as sent by host:list-forward")
{
    const auto entries = parseForwardList("emulator-5556 tcp:10282 localabstract:gs_probe\n"
                                          "192.168.1.20:5555 tcp:2963 localabstract:gamescrcpy\n");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0] == ForwardEntry{"emulator-5556", "tcp:10282", "localabstract:gs_probe"});
    CHECK(entries[1] == ForwardEntry{"192.168.1.20:5555", "tcp:2963", "localabstract:gamescrcpy"});
}

TEST_CASE("reverse list as sent by reverse:list-forward")
{
    const auto entries = parseReverseList("host-17 localabstract:gs_rev tcp:27999\n");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0] == ForwardEntry{"host-17", "tcp:27999", "localabstract:gs_rev"});
}

TEST_CASE("forward list: blank, short and long lines are skipped")
{
    CHECK(parseForwardList("").empty());
    CHECK(parseForwardList("\n  \n").empty());
    CHECK(parseForwardList("a b\n").empty());
    CHECK(parseForwardList("a b c d\n").empty());
    const auto entries = parseForwardList("  a   b\tc  \r\nx y\n");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0] == ForwardEntry{"a", "b", "c"});
}
