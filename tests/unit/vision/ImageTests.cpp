#include "TestImages.h"

#include <gs/vision/Glance.h>
#include <gs/vision/Image.h>
#include <gs/vision/IntegralImage.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace gs::vision;
using gs::vision::testing::Dice;
using gs::vision::testing::scene;

TEST_CASE("vision image: a crop is a view into the same pixels, clipped to the image")
{
    const Image image = scene(40, 30, 1);
    const ImageView inside = image.view().crop({5, 7, 10, 4});
    CHECK(inside.width() == 10);
    CHECK(inside.height() == 4);
    CHECK(inside.stride() == 40);
    CHECK(inside.at(0, 0) == image.at(5, 7));
    CHECK(inside.at(9, 3) == image.at(14, 10));

    const ImageView clipped = image.view().crop({35, -5, 20, 10});
    CHECK(clipped.width() == 5);
    CHECK(clipped.height() == 5);
    CHECK(clipped.at(0, 0) == image.at(35, 0));
    CHECK(image.view().crop({50, 50, 4, 4}).empty());
    CHECK(Image(0, 7).empty());
    CHECK(Image(3, 0).width() == 0);
}

TEST_CASE("vision image: rectangles intersect and overlap as intersection over union")
{
    CHECK(intersect({0, 0, 10, 10}, {5, 5, 10, 10}) == Rect{5, 5, 5, 5});
    CHECK(intersect({0, 0, 10, 10}, {10, 0, 5, 5}).empty());
    CHECK(overlap({0, 0, 10, 10}, {0, 0, 10, 10}) == doctest::Approx(1.0));
    CHECK(overlap({0, 0, 10, 10}, {5, 0, 10, 10}) == doctest::Approx(50.0 / 150.0));
    CHECK(overlap({0, 0, 10, 10}, {20, 20, 10, 10}) == 0.0);
}

TEST_CASE("vision image: halving averages each 2x2 block, rounding half up, and drops an odd edge")
{
    Image image(5, 3);
    const std::uint8_t values[] = {0, 1, 2, 3, 9, 4, 6, 255, 255, 9, 7, 7, 7, 7, 7};
    for (int index = 0; index < 15; ++index) {
        image.at(index % 5, index / 5) = values[index];
    }
    for (const Simd simd : {Simd::Scalar, Simd::Avx2}) {
        const Image half = downsample(image.view(), simd);
        REQUIRE(half.width() == 2);
        REQUIRE(half.height() == 1);
        CHECK(half.at(0, 0) == 3);
        CHECK(half.at(1, 0) == 129);
    }
    CHECK(downsample(Image(1, 9).view()).empty());
}

TEST_CASE("vision image: the AVX2 halving equals the scalar one for every row width")
{
    if (!avx2Available()) {
        MESSAGE("this CPU has no AVX2: only the scalar path runs");
        return;
    }
    Dice dice(7);
    for (int width = 2; width <= 160; ++width) {
        CAPTURE(width);
        Image image(width, 6);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < width; ++x) {
                image.at(x, y) = static_cast<std::uint8_t>(dice.below(256));
            }
        }
        const Image scalar = downsample(image.view(), Simd::Scalar);
        const Image fast = downsample(image.view(), Simd::Avx2);
        REQUIRE(scalar.width() == fast.width());
        CHECK(std::vector<std::uint8_t>(scalar.data(), scalar.data() + scalar.width() * scalar.height()) ==
              std::vector<std::uint8_t>(fast.data(), fast.data() + fast.width() * fast.height()));
    }
}

TEST_CASE("vision image: integral images give the sum and the sum of squares of any rectangle")
{
    const Image image = scene(37, 23, 3);
    const IntegralImage integral(image.view());
    Dice dice(11);
    for (int trial = 0; trial < 200; ++trial) {
        const int x = dice.below(37);
        const int y = dice.below(23);
        const Rect area{x, y, 1 + dice.below(37 - x), 1 + dice.below(23 - y)};
        std::int64_t sum = 0;
        std::int64_t squares = 0;
        for (int row = area.y; row < area.y + area.height; ++row) {
            for (int column = area.x; column < area.x + area.width; ++column) {
                sum += image.at(column, row);
                squares += image.at(column, row) * image.at(column, row);
            }
        }
        CHECK(integral.sum(area) == sum);
        CHECK(integral.squareSum(area) == squares);
    }
    CHECK(integral.sum({0, 0, 0, 0}) == 0);
}

TEST_CASE("vision image: a glance averages a region down to at most 64 a side, and counts the share that changed")
{
    Image flat(200, 100, 100);
    const Glance before = glanceOf(flat.view());
    CHECK(before.width == 64);
    CHECK(before.height == 32);
    CHECK(std::ranges::all_of(before.luma, [](std::uint8_t value) { return value == 100; }));
    CHECK(glanceOf(flat.view().crop({10, 10, 20, 5})).width == 20);
    CHECK(glanceOf(ImageView()).empty());

    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; ++x) {
            flat.at(x, y) = static_cast<std::uint8_t>(100 + ((x + y) % 2 == 0 ? 10 : -10));
        }
    }
    CHECK(changedShare(before, glanceOf(flat.view())) == 0.0);
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 100; ++x) {
            flat.at(x, y) = 200;
        }
    }
    const double moved = changedShare(before, glanceOf(flat.view()));
    CHECK(moved > 0.2);
    CHECK(moved < 0.3);
    CHECK(changedShare(before, glanceOf(flat.view().crop({0, 0, 100, 100}))) == 1.0);
    CHECK(changedShare(Glance{}, before) == 1.0);
}
