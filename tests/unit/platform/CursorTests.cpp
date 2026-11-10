#include <gs/platform/Cursor.h>

#include <doctest/doctest.h>

#include <limits>

using gs::platform::pinnedArea;
using gs::platform::ScreenArea;

TEST_CASE("ADR-0069: a locked view pins the cursor to one pixel at a share of the window, never outside it")
{
    const ScreenArea client{100, 200, 800, 600};
    const std::optional<ScreenArea> middle = pinnedArea(client, 0.5, 0.5);
    REQUIRE(middle.has_value());
    CHECK(middle->x == 500);
    CHECK(middle->y == 500);
    CHECK(middle->width == 1);
    CHECK(middle->height == 1);

    const std::optional<ScreenArea> corner = pinnedArea(client, 1.0, 1.0);
    REQUIRE(corner.has_value());
    CHECK(corner->x == 899);
    CHECK(corner->y == 799);

    const std::optional<ScreenArea> outside = pinnedArea(client, -0.5, 2.0);
    REQUIRE(outside.has_value());
    CHECK(outside->x == 100);
    CHECK(outside->y == 799);

    CHECK_FALSE(pinnedArea(ScreenArea{0, 0, 0, 600}, 0.5, 0.5).has_value());
    CHECK_FALSE(pinnedArea(client, std::numeric_limits<double>::quiet_NaN(), 0.5).has_value());
}
