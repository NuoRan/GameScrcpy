#include "TestImages.h"

#include <gs/vision/Matcher.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace gs::vision;
using gs::vision::testing::crop;
using gs::vision::testing::Dice;
using gs::vision::testing::disc;
using gs::vision::testing::paste;
using gs::vision::testing::scene;

namespace {

Template templateOf(const Image& pixels, const Image* mask = nullptr)
{
    auto made = Template::make(pixels.view(), mask != nullptr ? mask->view() : ImageView());
    REQUIRE(made);
    return std::move(*made);
}

std::vector<Simd> paths()
{
    return avx2Available() ? std::vector<Simd>{Simd::Scalar, Simd::Avx2} : std::vector<Simd>{Simd::Scalar};
}

}

TEST_CASE("vision matcher: templates cut from a scene are found exactly where they were cut")
{
    const Image image = scene(320, 240, 21);
    const Rect areas[] = {
        {0, 0, 48, 48},     {272, 192, 48, 48}, {101, 37, 96, 96}, {13, 150, 40, 24},
        {200, 3, 17, 61},   {150, 120, 64, 32}, {7, 203, 120, 37}, {255, 0, 65, 240},
    };
    for (const Simd simd : paths()) {
        for (const Rect& area : areas) {
            CAPTURE(area.x);
            CAPTURE(area.y);
            CAPTURE(area.width);
            CAPTURE(area.height);
            const Template pattern = templateOf(crop(image, area));
            const std::optional<Match> found = findBest(image.view(), pattern, {.simd = simd});
            REQUIRE(found);
            CHECK(found->rect == area);
            CHECK(found->score > 0.999999);
            CHECK(found->level >= 0);
            CHECK(found->level < pattern.levelCount());
        }
    }
}

TEST_CASE("vision matcher: the pyramid picks the same place as the exhaustive search")
{
    const Image image = scene(256, 200, 5);
    Image shifted = scene(256, 200, 6);
    const Rect source{60, 40, 64, 64};
    const Image pixels = crop(image, source);
    paste(shifted, pixels, 133, 97);
    Dice dice(9);
    for (int y = 97; y < 97 + 64; ++y) {
        for (int x = 133; x < 133 + 64; ++x) {
            shifted.at(x, y) = static_cast<std::uint8_t>(std::clamp(shifted.at(x, y) + dice.below(21) - 10, 0, 255));
        }
    }
    const Template pattern = templateOf(pixels);
    const std::optional<Match> exhaustive = findBest(shifted.view(), pattern, {.maxLevel = 0});
    const std::optional<Match> pyramid = findBest(shifted.view(), pattern);
    REQUIRE(exhaustive);
    REQUIRE(pyramid);
    CHECK(exhaustive->rect == Rect{133, 97, 64, 64});
    CHECK(exhaustive->level == 0);
    CHECK(pyramid->rect == exhaustive->rect);
    CHECK(pyramid->score == exhaustive->score);
    CHECK(pyramid->level > 0);
    CHECK(pyramid->score > 0.9);
}

TEST_CASE("vision matcher: the score is the normalised cross-correlation of OpenCV's TM_CCOEFF_NORMED")
{
    const Image image = scene(60, 50, 2);
    const Image pixels = crop(scene(60, 50, 3), {10, 10, 21, 17});
    const Template pattern = templateOf(pixels);
    const ScoreMap map = scoreMap(image.view(), pattern, Simd::Scalar);
    REQUIRE(map.width == 40);
    REQUIRE(map.height == 34);
    const auto direct = [&](int px, int py) {
        double meanT = 0.0;
        double meanI = 0.0;
        const double n = 21.0 * 17.0;
        for (int y = 0; y < 17; ++y) {
            for (int x = 0; x < 21; ++x) {
                meanT += pixels.at(x, y) / n;
                meanI += image.at(px + x, py + y) / n;
            }
        }
        double cross = 0.0;
        double varT = 0.0;
        double varI = 0.0;
        for (int y = 0; y < 17; ++y) {
            for (int x = 0; x < 21; ++x) {
                const double t = pixels.at(x, y) - meanT;
                const double i = image.at(px + x, py + y) - meanI;
                cross += t * i;
                varT += t * t;
                varI += i * i;
            }
        }
        return cross / std::sqrt(varT * varI);
    };
    for (const auto& [x, y] : {std::pair{0, 0}, std::pair{39, 33}, std::pair{17, 5}, std::pair{3, 29}}) {
        CAPTURE(x);
        CAPTURE(y);
        CHECK(map.at(x, y) == doctest::Approx(direct(x, y)).epsilon(1e-9));
    }
}

TEST_CASE("vision matcher: the AVX2 path gives the same scores as the scalar reference")
{
    if (!avx2Available()) {
        MESSAGE("this CPU has no AVX2: only the scalar path runs");
        return;
    }
    const Image image = scene(121, 93, 13);
    Dice dice(17);
    for (int trial = 0; trial < 10; ++trial) {
        const int width = 1 + dice.below(45);
        const int height = 1 + dice.below(35);
        const Rect area{dice.below(121 - width), dice.below(93 - height), width, height};
        CAPTURE(width);
        CAPTURE(height);
        const Image pixels = crop(scene(121, 93, 100 + static_cast<std::uint32_t>(trial)), area);
        const Image mask = disc(width, height);
        const Template plain = templateOf(pixels);
        const Template masked = templateOf(pixels, &mask);
        for (const Template* pattern : {&plain, &masked}) {
            const ScoreMap scalar = scoreMap(image.view(), *pattern, Simd::Scalar);
            const ScoreMap fast = scoreMap(image.view(), *pattern, Simd::Avx2);
            CHECK(scalar.width == fast.width);
            CHECK(scalar.scores == fast.scores);
        }
    }
}

TEST_CASE("vision matcher: a region of interest limits the search and keeps image coordinates")
{
    const Image image = scene(300, 200, 31);
    Image repeated = image;
    const Image icon = crop(image, {20, 30, 32, 32});
    paste(repeated, icon, 220, 140);
    const Template pattern = templateOf(icon);
    for (const Simd simd : paths()) {
        const auto everywhere = findAll(repeated.view(), pattern, {.simd = simd});
        REQUIRE(everywhere.size() == 2);
        CHECK(everywhere[0].rect == Rect{20, 30, 32, 32});
        CHECK(everywhere[1].rect == Rect{220, 140, 32, 32});

        const auto right = findBest(repeated.view(), pattern, {.roi = {150, 100, 150, 100}, .simd = simd});
        REQUIRE(right);
        CHECK(right->rect == Rect{220, 140, 32, 32});

        const auto tight = findBest(repeated.view(), pattern, {.roi = {220, 140, 32, 32}, .simd = simd});
        REQUIRE(tight);
        CHECK(tight->rect == Rect{220, 140, 32, 32});

        const auto beside =
            findBest(repeated.view(), pattern, {.roi = {221, 140, 32, 32}, .threshold = -1.0, .simd = simd});
        REQUIRE(beside);
        CHECK(beside->rect == Rect{221, 140, 32, 32});
        CHECK(beside->score < 0.999);
        CHECK_FALSE(findBest(repeated.view(), pattern, {.roi = {100, 100, 31, 90}, .threshold = -1.0, .simd = simd}));

        const auto clipped = findBest(repeated.view(), pattern, {.roi = {200, 120, 500, 500}, .simd = simd});
        REQUIRE(clipped);
        CHECK(clipped->rect == Rect{220, 140, 32, 32});
    }
}

TEST_CASE("vision matcher: the threshold decides between a match and none")
{
    const Image image = scene(200, 150, 41);
    const Template stranger = templateOf(crop(scene(200, 150, 42), {50, 50, 40, 40}));
    CHECK_FALSE(findBest(image.view(), stranger));
    CHECK(findAll(image.view(), stranger).empty());
    const auto anything = findBest(image.view(), stranger, {.threshold = -1.0});
    REQUIRE(anything);
    CHECK(anything->score < 0.8);
    CHECK(anything->score >= -1.0);
}

TEST_CASE("vision matcher: repeated icons give one match each, best first")
{
    Image image = scene(400, 300, 51);
    const Image icon = crop(scene(64, 64, 52), {8, 8, 36, 36});
    const Rect places[] = {{10, 10, 36, 36},  {61, 12, 36, 36},  {113, 9, 36, 36},
                           {300, 200, 36, 36}, {201, 151, 36, 36}, {17, 251, 36, 36}};
    for (const Rect& place : places) {
        paste(image, icon, place.x, place.y);
    }
    const Template pattern = templateOf(icon);
    for (const Simd simd : paths()) {
        std::vector<Match> found = findAll(image.view(), pattern, {.simd = simd});
        REQUIRE(found.size() == std::size(places));
        for (const Match& match : found) {
            CHECK(match.score > 0.999999);
        }
        std::ranges::sort(found, [](const Match& a, const Match& b) {
            return a.rect.y != b.rect.y ? a.rect.y < b.rect.y : a.rect.x < b.rect.x;
        });
        std::vector<Rect> expected(std::begin(places), std::end(places));
        std::ranges::sort(expected, [](const Rect& a, const Rect& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
        for (std::size_t index = 0; index < expected.size(); ++index) {
            CHECK(found[index].rect == expected[index]);
        }
        CHECK(findAll(image.view(), pattern, {.maxResults = 4, .simd = simd}).size() == 4);
        CHECK(findAll(image.view(), pattern, {.maxResults = 0, .simd = simd}).empty());
    }
}

TEST_CASE("vision matcher: overlapping matches are suppressed by intersection over union")
{
    Image image = scene(200, 120, 61);
    const Image icon = crop(scene(80, 80, 62), {0, 0, 40, 40});
    paste(image, icon, 30, 40);
    Image second = image;
    paste(second, icon, 130, 40);
    const Template pattern = templateOf(icon);
    const auto apart = findAll(second.view(), pattern, {.threshold = 0.95, .maxOverlap = 0.0});
    CHECK(apart.size() == 2);
    const auto near = findAll(image.view(), pattern, {.threshold = 0.5, .maxOverlap = 0.3});
    REQUIRE(!near.empty());
    for (std::size_t a = 0; a < near.size(); ++a) {
        for (std::size_t b = a + 1; b < near.size(); ++b) {
            CHECK(overlap(near[a].rect, near[b].rect) <= 0.3);
        }
    }
    CHECK(near.front().rect == Rect{30, 40, 40, 40});
}

TEST_CASE("vision matcher: a mask keeps transparent pixels out of the score")
{
    const Image source = scene(160, 120, 71);
    const Rect area{40, 30, 56, 56};
    const Image pixels = crop(source, area);
    const Image mask = disc(56, 56);
    Image target = scene(240, 180, 72);
    paste(target, pixels, 150, 90, &mask);

    const Template masked = templateOf(pixels, &mask);
    const Template plain = templateOf(pixels);
    CHECK(masked.masked());
    CHECK_FALSE(plain.masked());
    for (const Simd simd : paths()) {
        const auto found = findBest(target.view(), masked, {.simd = simd});
        REQUIRE(found);
        CHECK(found->rect == Rect{150, 90, 56, 56});
        CHECK(found->score > 0.999999);
        const auto unmasked = findBest(target.view(), plain, {.threshold = -1.0, .simd = simd});
        REQUIRE(unmasked);
        CHECK(unmasked->score < 0.95);
    }

    Image repainted = pixels;
    for (int y = 0; y < 56; ++y) {
        for (int x = 0; x < 56; ++x) {
            if (mask.at(x, y) == 0) {
                repainted.at(x, y) = static_cast<std::uint8_t>(255 - repainted.at(x, y));
            }
        }
    }
    const Template other = templateOf(repainted, &mask);
    const Image small = crop(target, {120, 60, 100, 100});
    CHECK(scoreMap(small.view(), masked).scores == scoreMap(small.view(), other).scores);
}

TEST_CASE("vision matcher: a fully opaque mask is the same as no mask")
{
    const Image pixels = crop(scene(100, 100, 81), {10, 10, 30, 30});
    const Image solid(30, 30, 255);
    const Template masked = templateOf(pixels, &solid);
    CHECK_FALSE(masked.masked());
    const Image image = scene(100, 100, 81);
    CHECK(scoreMap(image.view(), masked).scores == scoreMap(image.view(), templateOf(pixels)).scores);
}

TEST_CASE("vision matcher: flat templates and flat windows score 0 instead of dividing by zero")
{
    const Image flat(24, 24, 128);
    const Template blank = templateOf(flat);
    CHECK(blank.flat());
    const Image image = scene(120, 90, 91);
    CHECK_FALSE(findBest(image.view(), blank, {.threshold = -1.0}));
    CHECK(findAll(image.view(), blank, {.threshold = -1.0}).empty());
    const ScoreMap blankMap = scoreMap(image.view(), blank);
    CHECK(std::ranges::all_of(blankMap.scores, [](double score) { return score == 0.0; }));

    const Template detailed = templateOf(crop(image, {10, 10, 24, 24}));
    const Image wall(80, 60, 30);
    const ScoreMap wallMap = scoreMap(wall.view(), detailed);
    REQUIRE(wallMap.width == 57);
    CHECK(std::ranges::all_of(wallMap.scores, [](double score) { return score == 0.0; }));
    CHECK_FALSE(findBest(wall.view(), detailed, {.threshold = 0.01}));
}

TEST_CASE("vision matcher: bad templates are refused with a reason")
{
    const Image pixels = scene(20, 20, 99);
    CHECK(Template::make(ImageView()).error() == TemplateError::Empty);
    const Image smallMask(19, 20, 255);
    CHECK(Template::make(pixels.view(), smallMask.view()).error() == TemplateError::MaskSize);
    const Image clear(20, 20, 0);
    CHECK(Template::make(pixels.view(), clear.view()).error() == TemplateError::Transparent);
    const Image wide(kMaxTemplateSide + 1, 1, 7);
    CHECK(Template::make(wide.view()).error() == TemplateError::TooLarge);
    const Template none;
    CHECK(none.empty());
    CHECK_FALSE(findBest(pixels.view(), none));
}

TEST_CASE("vision matcher: the number of pyramid levels follows the template size")
{
    const Image image = scene(400, 400, 111);
    CHECK(templateOf(crop(image, {0, 0, 96, 96})).levelCount() == 4);
    CHECK(templateOf(crop(image, {0, 0, 48, 48})).levelCount() == 3);
    CHECK(templateOf(crop(image, {0, 0, 24, 24})).levelCount() == 2);
    CHECK(templateOf(crop(image, {0, 0, 16, 16})).levelCount() == 1);
    CHECK(templateOf(crop(image, {0, 0, 200, 20})).levelCount() == 2);
    CHECK(templateOf(crop(image, {0, 0, 400, 400})).levelCount() == 6);
    const Template levels = templateOf(crop(image, {0, 0, 97, 51}));
    REQUIRE(levels.levelCount() == 3);
    CHECK(levels.level(1).width == 48);
    CHECK(levels.level(1).height == 25);
    CHECK(levels.level(2).width == 24);
    CHECK(levels.level(2).height == 12);
}

TEST_CASE("vision matcher: the same search twice gives the same answer")
{
    const Image image = scene(300, 220, 121);
    const Template pattern = templateOf(crop(scene(300, 220, 122), {100, 100, 50, 40}));
    const MatchOptions options{.threshold = -1.0, .maxResults = 10};
    CHECK(findAll(image.view(), pattern, options) == findAll(image.view(), pattern, options));
    CHECK(findBest(image.view(), pattern, options) == findBest(image.view(), pattern, options));
}

TEST_CASE("vision matcher: the vectorised column sums agree with the reference on wide and tall templates")
{
    if (!avx2Available()) {
        MESSAGE("this CPU has no AVX2: only the scalar path runs");
        return;
    }
    const Image image = scene(341, 260, 131);
    const Rect areas[] = {{0, 0, 97, 5}, {3, 7, 33, 64}, {10, 10, 12, 150}, {0, 0, 128, 21}, {40, 30, 7, 200}};
    for (const Rect& area : areas) {
        CAPTURE(area.width);
        CAPTURE(area.height);
        const Template pattern = templateOf(crop(image, area));
        const ScoreMap scalar = scoreMap(image.view(), pattern, Simd::Scalar);
        const ScoreMap fast = scoreMap(image.view(), pattern, Simd::Avx2);
        CHECK(scalar.width == fast.width);
        CHECK(scalar.height == fast.height);
        CHECK(scalar.scores == fast.scores);
    }
}

TEST_CASE("vision matcher: a large frame is searched the same way twice and from several threads")
{
    if (!avx2Available()) {
        MESSAGE("this CPU has no AVX2: only the scalar path runs");
        return;
    }
    const Image image = scene(720, 540, 141);
    const Rect target{400, 300, 48, 48};
    const Template pattern = templateOf(crop(image, target));
    const std::vector<Match> first = findAll(image.view(), pattern, {.simd = Simd::Avx2});
    REQUIRE(!first.empty());
    CHECK(first.front().rect == target);
    CHECK(first.front().score > 0.999999);
    CHECK(findAll(image.view(), pattern, {.simd = Simd::Avx2}) == first);

    std::vector<std::vector<Match>> answers(4);
    std::vector<std::thread> runners;
    for (int index = 0; index < 4; ++index) {
        runners.emplace_back([&image, &pattern, &answers, index] {
            for (int round = 0; round < 3; ++round) {
                answers[static_cast<std::size_t>(index)] =
                    findAll(image.view(), pattern, {.simd = Simd::Avx2});
            }
        });
    }
    for (std::thread& runner : runners) {
        runner.join();
    }
    for (const std::vector<Match>& answer : answers) {
        CHECK(answer == first);
    }
}

TEST_CASE("vision matcher: a match on the first or the last row of any band is found exactly")
{
    const Image image = scene(400, 300, 161);
    constexpr int kSide = 32;
    const int rows = image.height() - kSide + 1;
    std::vector<int> edges;
    for (int bands = 2; bands <= 5; ++bands) {
        for (int band = 1; band < bands; ++band) {
            const int first = rows * band / bands;
            edges.push_back(first);
            edges.push_back(first - 1);
        }
    }
    std::ranges::sort(edges);
    const auto repeated = std::ranges::unique(edges);
    edges.erase(repeated.begin(), repeated.end());
    for (const int y : edges) {
        CAPTURE(y);
        const Rect target{150, y, kSide, kSide};
        const std::optional<Match> found = findBest(image.view(), templateOf(crop(image, target)), {.maxLevel = 0});
        REQUIRE(found);
        CHECK(found->rect == target);
    }
}

TEST_CASE("vision matcher: a large template is found exactly on a large frame")
{
    const Image image = scene(900, 700, 151);
    const Rect target{300, 200, 200, 150};
    const Template pattern = templateOf(crop(image, target));
    for (const Simd simd : paths()) {
        const std::optional<Match> found = findBest(image.view(), pattern, {.simd = simd});
        REQUIRE(found);
        CHECK(found->rect == target);
        CHECK(found->score > 0.999999);
    }
}
