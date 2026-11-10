#include "TestImages.h"

#include <gs/vision/Color.h>
#include <gs/vision/Matcher.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

using namespace gs::vision;
using gs::vision::testing::Dice;

namespace {

struct Picture {
    Picture(int width, int height) : luma(width, height), color(width, height) {}

    void paint(int x, int y, std::uint8_t light, std::array<std::uint8_t, 3> rgb)
    {
        luma.at(x, y) = light;
        std::uint8_t* pixel = color.at(x, y);
        pixel[0] = rgb[0];
        pixel[1] = rgb[1];
        pixel[2] = rgb[2];
    }

    void button(int left, int top, std::array<std::uint8_t, 3> face, std::array<std::uint8_t, 3> mark)
    {
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 48; ++x) {
                const bool inner = x >= 12 && x < 36 && y >= 6 && y < 18 && (x + y) % 5 != 0;
                paint(left + x, top + y, inner ? 60 : 150, inner ? mark : face);
            }
        }
    }

    Image luma;
    ColorImage color;
};

Picture backdrop(int width, int height, std::uint32_t seed)
{
    Picture picture(width, height);
    Dice dice(seed);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto shade = static_cast<std::uint8_t>(20 + dice.below(40));
            picture.paint(x, y, shade, {shade, shade, shade});
        }
    }
    return picture;
}

Template colorTemplate(const Picture& picture, const Rect& area)
{
    const Image pixels = Image::copyOf(picture.luma.view().crop(area));
    const ColorImage colors = ColorImage::copyOf(picture.color.view().crop(area));
    auto made = Template::make(pixels.view(), {}, colors.view());
    REQUIRE(made);
    return std::move(*made);
}

constexpr std::array<std::uint8_t, 3> kRed{220, 60, 50};
constexpr std::array<std::uint8_t, 3> kDarkRed{90, 20, 20};
constexpr std::array<std::uint8_t, 3> kTeal{40, 150, 170};
constexpr std::array<std::uint8_t, 3> kDarkTeal{10, 60, 70};

}

TEST_CASE("vision color: color mode tells apart two buttons of the same shape and different color, grey mode does not")
{
    Picture picture = backdrop(240, 160, 7);
    picture.button(30, 40, kRed, kDarkRed);
    picture.button(150, 100, kTeal, kDarkTeal);
    const Template red = colorTemplate(picture, {30, 40, 48, 24});
    const Template teal = colorTemplate(picture, {150, 100, 48, 24});
    CHECK(red.colored());
    CHECK(red.color().width() == 48);

    const std::vector<Match> grey = findAll(picture.luma.view(), red, {.threshold = 0.9});
    REQUIRE(grey.size() == 2);
    CHECK(grey[0].score > 0.999);
    CHECK(grey[1].score > 0.999);

    const std::vector<Match> reds = findAll(picture.luma.view(), picture.color.view(), red, {.threshold = 0.9});
    REQUIRE(reds.size() == 1);
    CHECK(reds.front().rect == Rect{30, 40, 48, 24});
    CHECK(reds.front().score > 0.999);
    const std::optional<Match> tealBest = findBest(picture.luma.view(), picture.color.view(), teal, {.threshold = 0.9});
    REQUIRE(tealBest);
    CHECK(tealBest->rect == Rect{150, 100, 48, 24});

    const std::optional<Match> anything =
        findBest(picture.luma.view(), picture.color.view(), red, {.roi = {140, 90, 70, 40}, .threshold = -1.0});
    REQUIRE(anything);
    CHECK(anything->rect == Rect{150, 100, 48, 24});
    CHECK(anything->score < 0.3);
    CHECK_FALSE(findBest(picture.luma.view(), picture.color.view(), red, {.roi = {140, 90, 70, 40}}));

    const std::optional<Match> withoutColor = findBest(picture.luma.view(), ColorView(), red, {.roi = {140, 90, 70, 40}});
    REQUIRE(withoutColor);
    CHECK(withoutColor->rect == Rect{150, 100, 48, 24});
    const std::optional<Match> greyPattern =
        findBest(picture.luma.view(), picture.color.view(), *Template::make(Image::copyOf(picture.luma.view().crop({30, 40, 48, 24})).view()),
                 {.roi = {140, 90, 70, 40}});
    REQUIRE(greyPattern);
    CHECK(greyPattern->rect == Rect{150, 100, 48, 24});
}

TEST_CASE("vision color: the color likeness is 1 - d / 128, d the mean absolute difference of red, green and blue")
{
    Picture picture = backdrop(120, 80, 9);
    picture.button(10, 10, kRed, kDarkRed);
    picture.button(60, 40, kTeal, kDarkTeal);
    const Template red = colorTemplate(picture, {10, 10, 48, 24});
    CHECK(colorLikeness(picture.color.view(), red, 10, 10) == 1.0);
    std::int64_t difference = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 48; ++x) {
            const std::uint8_t* want = picture.color.view().at(10 + x, 10 + y);
            const std::uint8_t* seen = picture.color.view().at(60 + x, 40 + y);
            for (int channel = 0; channel < 3; ++channel) {
                difference += std::abs(want[channel] - seen[channel]);
            }
        }
    }
    const double mean = static_cast<double>(difference) / (3.0 * 48 * 24);
    CHECK(colorLikeness(picture.color.view(), red, 60, 40) == doctest::Approx(std::max(0.0, 1.0 - mean / 128.0)));
    CHECK(colorLikeness(picture.color.view(), red, 100, 70) == 0.0);
    CHECK(colorLikeness(ColorView(), red, 10, 10) == 0.0);

    Picture shifted = picture;
    for (int y = 10; y < 34; ++y) {
        for (int x = 10; x < 58; ++x) {
            std::uint8_t* pixel = shifted.color.at(x, y);
            pixel[0] = static_cast<std::uint8_t>(pixel[0] + 8);
        }
    }
    CHECK(colorLikeness(shifted.color.view(), red, 10, 10) == doctest::Approx(1.0 - (8.0 / 3.0) / 128.0));
}

TEST_CASE("vision color: transparent pixels of a color template are left out, and sizes must agree")
{
    Picture picture = backdrop(160, 100, 11);
    picture.button(40, 30, kRed, kDarkRed);
    const Image pixels = Image::copyOf(picture.luma.view().crop({40, 30, 48, 24}));
    const ColorImage colors = ColorImage::copyOf(picture.color.view().crop({40, 30, 48, 24}));
    Image mask(48, 24, 255);
    for (int x = 0; x < 48; ++x) {
        mask.at(x, 0) = 0;
    }
    const auto masked = Template::make(pixels.view(), mask.view(), colors.view());
    REQUIRE(masked);
    Picture repainted = picture;
    for (int x = 40; x < 88; ++x) {
        repainted.paint(x, 30, 150, {0, 255, 0});
    }
    CHECK(colorLikeness(repainted.color.view(), *masked, 40, 30) == 1.0);
    const ColorImage narrow = ColorImage::copyOf(picture.color.view().crop({40, 30, 47, 24}));
    CHECK(Template::make(pixels.view(), {}, narrow.view()).error() == TemplateError::ColorSize);
    CHECK_FALSE(Template::make(pixels.view()).value().colored());
}

TEST_CASE("vision findColor: the largest group of matching pixels, joined diagonally, within the tolerance and region")
{
    ColorImage image(64, 48);
    const auto put = [&image](int x, int y, std::array<std::uint8_t, 3> rgb) {
        std::uint8_t* pixel = image.at(x, y);
        pixel[0] = rgb[0];
        pixel[1] = rgb[1];
        pixel[2] = rgb[2];
    };
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            put(x, y, {30, 30, 30});
        }
    }
    const Rgb gold{245, 197, 66};
    for (int y = 5; y < 9; ++y) {
        for (int x = 4; x < 10; ++x) {
            put(x, y, {245, 197, 66});
        }
    }
    for (int i = 0; i < 12; ++i) {
        put(30 + i, 20 + i, {250, 190, 70});
    }
    put(60, 2, {245, 197, 66});

    const std::optional<ColorPatch> found = findColor(image.view(), gold);
    REQUIRE(found);
    CHECK(found->rect == Rect{4, 5, 6, 4});
    CHECK(found->pixels == 24);
    CHECK(found->score == 1.0);

    const std::optional<ColorPatch> diagonal = findColor(image.view(), gold, {.roi = {20, 10, 40, 38}});
    REQUIRE(diagonal);
    CHECK(diagonal->rect == Rect{30, 20, 12, 12});
    CHECK(diagonal->pixels == 12);
    CHECK(diagonal->score == doctest::Approx(12.0 / 144.0));

    CHECK(findColor(image.view(), gold, {.roi = {20, 10, 40, 38}, .tolerance = 0.01}) == std::nullopt);
    const std::optional<ColorPatch> lone = findColor(image.view(), gold, {.roi = {50, 0, 14, 5}, .tolerance = 0.0});
    REQUIRE(lone);
    CHECK(lone->rect == Rect{60, 2, 1, 1});
    CHECK_FALSE(findColor(image.view(), Rgb{0, 0, 255}, {.tolerance = 0.2}));
    CHECK_FALSE(findColor(ColorView(), gold));

    for (int x = 4; x < 10; ++x) {
        put(x, 30, {245, 197, 66});
        put(x, 31, {245, 197, 66});
        put(x, 32, {245, 197, 66});
        put(x, 33, {245, 197, 66});
    }
    const std::optional<ColorPatch> tie = findColor(image.view(), gold, {.roi = {0, 0, 20, 48}});
    REQUIRE(tie);
    CHECK(tie->rect == Rect{4, 5, 6, 4});

    CHECK(colorReach(0.1) == 25);
    CHECK(colorReach(1.0) == 255);
    CHECK(colorReach(0.0) == 0);
    CHECK(colorReach(-1.0) == 0);
    const std::array<std::uint8_t, 3> near{255, 172, 91};
    CHECK(colorMatches(near.data(), gold, 25));
    CHECK_FALSE(colorMatches(near.data(), gold, 24));
}
