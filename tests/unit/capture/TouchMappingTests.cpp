#include "HidTouchscreen.h"

#include <gs/capture/CropDetector.h>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>

using gs::app::UhidPosition;
using gs::capture::ContentRect;
using gs::capture::CropBox;

namespace {

constexpr int kFrameWidth = 1920;
constexpr int kFrameHeight = 1080;
constexpr std::uint16_t kHalf = 32767;

const CropBox kSideways{.x = 0.0, .y = 72.0 / kFrameHeight, .width = 1.0, .height = 936.0 / kFrameHeight};
const CropBox kUpright{.x = 708.0 / kFrameWidth, .y = 0.0, .width = 504.0 / kFrameWidth, .height = 1.0};

std::optional<UhidPosition> touchAt(const CropBox& crop, const ContentRect& shown, double x, double y, bool turnedRight)
{
    const std::optional<std::array<std::uint16_t, 2>> picture = gs::capture::pictureFixed(shown, x, y);
    if (!picture) {
        return std::nullopt;
    }
    return gs::app::naturalPosition((*picture)[0], (*picture)[1],
                                    gs::capture::rotationOf(crop, kFrameWidth, kFrameHeight, turnedRight));
}

}

TEST_CASE("a touch on the shown picture lands on the phone's own screen through the crop, turned left or right (MED-014)")
{
    const ContentRect shown{.x = 0.0, .y = 0.0, .width = 1920.0, .height = 936.0};

    CHECK(touchAt(kSideways, shown, 0.0, 0.0, false) == UhidPosition{.x = kHalf, .y = 0});
    CHECK(touchAt(kSideways, shown, 1920.0, 936.0, false) == UhidPosition{.x = 0, .y = kHalf});
    CHECK(touchAt(kSideways, shown, 0.0, 936.0, false) == UhidPosition{.x = 0, .y = 0});

    CHECK(touchAt(kSideways, shown, 0.0, 0.0, true) == UhidPosition{.x = 0, .y = kHalf});
    CHECK(touchAt(kSideways, shown, 1920.0, 936.0, true) == UhidPosition{.x = kHalf, .y = 0});
    CHECK(touchAt(kSideways, shown, 1920.0, 0.0, true) == UhidPosition{.x = 0, .y = 0});

    const std::optional<UhidPosition> middle = touchAt(kSideways, shown, 960.0, 468.0, false);
    REQUIRE(middle);
    CHECK(middle->x == doctest::Approx(kHalf / 2.0).epsilon(0.001));
    CHECK(middle->y == doctest::Approx(kHalf / 2.0).epsilon(0.001));

    CHECK_FALSE(touchAt(kSideways, shown, -1.0, 10.0, false));
    CHECK_FALSE(touchAt(kSideways, shown, 10.0, 937.0, false));
}

TEST_CASE("an upright phone in black bars takes touches as they are, scaled to its own screen (MED-014)")
{
    const ContentRect shown{.x = 300.0, .y = 20.0, .width = 504.0, .height = 1080.0};
    CHECK(gs::capture::rotationOf(kUpright, kFrameWidth, kFrameHeight, false) == 0);
    CHECK(gs::capture::rotationOf(kUpright, kFrameWidth, kFrameHeight, true) == 0);
    CHECK(touchAt(kUpright, shown, 300.0, 20.0, true) == UhidPosition{.x = 0, .y = 0});
    CHECK(touchAt(kUpright, shown, 804.0, 1100.0, false) == UhidPosition{.x = kHalf, .y = kHalf});
    CHECK_FALSE(touchAt(kUpright, shown, 299.0, 500.0, false));
    CHECK_FALSE(touchAt(kUpright, shown, 900.0, 500.0, false));
    CHECK_FALSE(gs::capture::pictureFixed(ContentRect{}, 0.0, 0.0));
}

TEST_CASE("a point of the cropped picture is found in the card's whole frame")
{
    const std::array<double, 2> corner = gs::capture::frameOf(kSideways, 0.0, 0.0);
    CHECK(corner[0] == doctest::Approx(0.0));
    CHECK(corner[1] == doctest::Approx(72.0 / kFrameHeight));
    const std::array<double, 2> far = gs::capture::frameOf(kSideways, 1.0, 1.0);
    CHECK(far[0] == doctest::Approx(1.0));
    CHECK(far[1] == doctest::Approx(1008.0 / kFrameHeight));
    const std::array<double, 2> centre = gs::capture::frameOf(kUpright, 0.5, 0.5);
    CHECK(centre[0] == doctest::Approx(960.0 / kFrameWidth));
    CHECK(centre[1] == doctest::Approx(0.5));
}
