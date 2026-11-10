#include "gs/input/Geometry.h"

#include <doctest/doctest.h>

#include <array>

using namespace gs::input;

namespace {

struct Pixels {
    double x;
    double y;
};

Pixels toPixels(Vec2 normalized, const ScreenGeometry& screen)
{
    return {normalized.x * screen.width, normalized.y * screen.height};
}

constexpr std::array<ScreenGeometry, 4> kLandscapeScreens{{
    {.width = 1920, .height = 1080},
    {.width = 2340, .height = 1080},
    {.width = 2400, .height = 1080},
    {.width = 1440, .height = 1080},
}};

}

TEST_CASE("corner-anchored controls keep their distance to the corner on every aspect ratio")
{
    for (const ScreenGeometry& screen : kLandscapeScreens) {
        INFO(screen.width << "x" << screen.height);
        const CoordinateMapper mapper{screen, ScaleMode::ShortSide, SafeArea::Ignore};
        const Pixels jump = toPixels(mapper.position(Anchor::BottomRight, {-0.20, -0.40}), screen);
        CHECK(screen.width - jump.x == doctest::Approx(216.0));
        CHECK(screen.height - jump.y == doctest::Approx(432.0));

        const Pixels move = toPixels(mapper.position(Anchor::BottomLeft, {0.36, -0.30}), screen);
        CHECK(move.x == doctest::Approx(388.8));
        CHECK(screen.height - move.y == doctest::Approx(324.0));
    }
}

TEST_CASE("a joystick radius describes a circle in pixels under shortSide scaling")
{
    for (const ScreenGeometry& screen : kLandscapeScreens) {
        INFO(screen.width << "x" << screen.height);
        const CoordinateMapper mapper{screen, ScaleMode::ShortSide, SafeArea::Ignore};
        const Vec2 radius = mapper.radius(0.12);
        CHECK(radius.x * screen.width == doctest::Approx(radius.y * screen.height));
        CHECK(radius.y * screen.height == doctest::Approx(129.6));
    }
}

TEST_CASE("portrait screens use the width as the short side")
{
    const ScreenGeometry screen{.width = 1080, .height = 2400};
    const CoordinateMapper mapper{screen, ScaleMode::ShortSide, SafeArea::Ignore};
    const Pixels point = toPixels(mapper.position(Anchor::Top, {0.0, 0.25}), screen);
    CHECK(point.x == doctest::Approx(540.0));
    CHECK(point.y == doctest::Approx(270.0));
}

TEST_CASE("stretch scaling measures x by the width and y by the height")
{
    const ScreenGeometry screen{.width = 2400, .height = 1080};
    const CoordinateMapper mapper{screen, ScaleMode::Stretch, SafeArea::Ignore};
    const Vec2 point = mapper.position(Anchor::TopLeft, {0.25, 0.5});
    CHECK(point.x == doctest::Approx(0.25));
    CHECK(point.y == doctest::Approx(0.5));
    const Vec2 size = mapper.extent({0.5, 0.5});
    CHECK(size.x == doctest::Approx(0.5));
    CHECK(size.y == doctest::Approx(0.5));
}

TEST_CASE("respecting the safe area measures anchors from the cutout-free rectangle")
{
    const ScreenGeometry screen{.width = 2400, .height = 1080, .safeLeft = 110};
    const CoordinateMapper ignoring{screen, ScaleMode::ShortSide, SafeArea::Ignore};
    const CoordinateMapper respecting{screen, ScaleMode::ShortSide, SafeArea::Respect};

    CHECK(toPixels(ignoring.position(Anchor::TopLeft, {0.1, 0.1}), screen).x == doctest::Approx(108.0));
    CHECK(toPixels(respecting.position(Anchor::TopLeft, {0.1, 0.1}), screen).x == doctest::Approx(218.0));
    CHECK(toPixels(respecting.position(Anchor::Right, {0.0, 0.0}), screen).x == doctest::Approx(2400.0));
    CHECK(toPixels(respecting.position(Anchor::Center, {0.0, 0.0}), screen).x == doctest::Approx(1255.0));
}

TEST_CASE("positions map back to the offset of any anchor, under both scalings and with the safe area")
{
    const ScreenGeometry notch{.width = 2400, .height = 1080, .safeLeft = 90, .safeRight = 30};
    for (const ScaleMode scale : {ScaleMode::ShortSide, ScaleMode::Stretch}) {
        for (const SafeArea area : {SafeArea::Ignore, SafeArea::Respect}) {
            const CoordinateMapper mapper{notch, scale, area};
            for (int anchor = 0; anchor < 9; ++anchor) {
                const auto at = static_cast<Anchor>(anchor);
                const Vec2 offset{0.12, -0.31};
                const Vec2 back = mapper.offsetOf(at, mapper.position(at, offset));
                CHECK(back.x == doctest::Approx(offset.x));
                CHECK(back.y == doctest::Approx(offset.y));
            }
            const Vec2 size{0.4, 0.25};
            const Vec2 measured = mapper.sizeOf(mapper.extent(size));
            CHECK(measured.x == doctest::Approx(size.x));
            CHECK(measured.y == doctest::Approx(size.y));
        }
    }
}

TEST_CASE("the anchor of a position is the third of the screen it falls in")
{
    const CoordinateMapper mapper{{.width = 2400, .height = 1080}, ScaleMode::ShortSide, SafeArea::Ignore};
    CHECK(mapper.anchorAt({0.1, 0.1}) == Anchor::TopLeft);
    CHECK(mapper.anchorAt({0.5, 0.2}) == Anchor::Top);
    CHECK(mapper.anchorAt({0.9, 0.3}) == Anchor::TopRight);
    CHECK(mapper.anchorAt({0.2, 0.5}) == Anchor::Left);
    CHECK(mapper.anchorAt({0.5, 0.5}) == Anchor::Center);
    CHECK(mapper.anchorAt({0.7, 0.6}) == Anchor::Right);
    CHECK(mapper.anchorAt({0.0, 1.0}) == Anchor::BottomLeft);
    CHECK(mapper.anchorAt({0.6, 0.9}) == Anchor::Bottom);
    CHECK(mapper.anchorAt({1.0, 0.99}) == Anchor::BottomRight);

    const CoordinateMapper respecting{{.width = 2400, .height = 1080, .safeLeft = 600}, ScaleMode::ShortSide,
                                      SafeArea::Respect};
    CHECK(respecting.anchorAt({0.3, 0.5}) == Anchor::Left);
    CHECK(respecting.anchorAt({0.55, 0.5}) == Anchor::Center);
}
