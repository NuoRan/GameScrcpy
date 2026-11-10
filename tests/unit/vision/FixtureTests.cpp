#include "TestImages.h"

#include <gs/vision/Matcher.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace gs::vision;
using gs::vision::testing::crop;
using gs::vision::testing::disc;
using gs::vision::testing::paste;
using gs::vision::testing::readPgm;
using gs::vision::testing::screen1080;

namespace {

struct Sighting {
    std::string_view pattern;
    std::string_view frame;
    int x = 0;
    int y = 0;
    double reference = 0.0;
    double threshold = 0.8;
};

constexpr Sighting kSightings[] = {
    {"search_bar", "h265_top", 20, 130, 1.0000},
    {"search_bar", "av1_top", 20, 136, 0.8636},
    {"search_bar", "h264_scrolled", 20, 51, 0.8737},
    {"search_bar", "av1_bottom", 20, 51, 0.8427},
    {"search_bar", "av1_landscape", 56, 35, 0.8790},
    {"search_bar", "h264_rotating", 497, 84, 0.2499},
    {"notifications_row", "h265_top", 16, 372, 1.0000},
    {"notifications_row", "av1_top", 16, 378, 0.8520},
    {"notifications_row", "h264_scrolled", 16, 132, 0.8602},
    {"notifications_row", "av1_bottom", 16, 112, 0.9382},
    {"notifications_row", "av1_landscape", 52, 251, 0.7800, 0.75},
    {"notifications_row", "h264_rotating", 49, 232, 0.1480},
    {"battery_row", "h265_top", 18, 434, 1.0000},
    {"battery_row", "av1_top", 18, 440, 0.9242},
    {"battery_row", "h264_scrolled", 18, 194, 0.9206},
    {"battery_row", "av1_bottom", 18, 174, 0.9520},
    {"battery_row", "av1_landscape", 55, 252, 0.3500},
    {"battery_row", "h264_rotating", 250, 254, 0.4443},
    {"profile", "h265_top", 236, 38, 1.0000},
    {"profile", "av1_top", 236, 44, 0.9272},
    {"profile", "h264_scrolled", 12, 191, 0.2870},
    {"profile", "av1_bottom", 12, 171, 0.2926},
    {"profile", "av1_landscape", 353, 130, 0.3976},
    {"profile", "h264_rotating", 112, 112, 0.3722},
    {"network_row", "h265_top", 16, 186, 1.0000},
    {"network_row", "av1_top", 16, 192, 0.8399},
    {"network_row", "h264_scrolled", 14, 562, 0.4428},
    {"network_row", "av1_bottom", 14, 542, 0.4426},
    {"network_row", "av1_landscape", 52, 65, 0.5381},
    {"network_row", "h264_rotating", 129, 38, 0.1413},
    {"bell", "h265_top", 21, 378, 1.0000},
    {"bell", "av1_top", 21, 384, 0.9361},
    {"bell", "h264_scrolled", 21, 138, 0.8579},
    {"bell", "av1_bottom", 21, 118, 0.9473},
    {"bell", "av1_landscape", 57, 257, 0.8574},
    {"bell", "h264_rotating", 222, 207, 0.5532},
    {"wallpaper_row", "h265_top", 14, 187, 0.3539},
    {"wallpaper_row", "av1_top", 14, 193, 0.3553},
    {"wallpaper_row", "h264_scrolled", 16, 440, 0.8973},
    {"wallpaper_row", "av1_bottom", 16, 420, 1.0000},
    {"wallpaper_row", "av1_landscape", 49, 128, 0.3240},
    {"wallpaper_row", "h264_rotating", 65, 232, 0.1609},
    {"location_row", "h265_top", 34, 367, 0.4004},
    {"location_row", "av1_top", 34, 373, 0.4092},
    {"location_row", "h264_scrolled", 16, 559, 0.3750},
    {"location_row", "av1_bottom", 16, 600, 1.0000},
    {"location_row", "av1_landscape", 54, 185, 0.3811},
    {"location_row", "h264_rotating", 464, 66, 0.3621},
};

constexpr double kReferenceTolerance = 0.0005;

const Image& fixture(std::string_view kind, std::string_view name)
{
    static std::map<std::string, Image, std::less<>> cache;
    const std::string file = std::string(kind) + "_" + std::string(name) + ".pgm";
    auto found = cache.find(file);
    if (found == cache.end()) {
        found = cache.emplace(file, readPgm(file)).first;
    }
    return found->second;
}

const Image& frameNamed(std::string_view name)
{
    return fixture("frame", name);
}

Template templateNamed(std::string_view name)
{
    auto made = Template::make(fixture("template", name).view());
    REQUIRE(made);
    return std::move(*made);
}

std::vector<std::uint8_t> pixelsOf(const Image& image)
{
    const std::uint8_t* first = image.data();
    return std::vector<std::uint8_t>(first, first + static_cast<std::ptrdiff_t>(image.width()) * image.height());
}

}

TEST_CASE("vision fixtures: templates are found where they appear in other frames and codecs, nowhere else")
{
    int found = 0;
    int missed = 0;
    int rejected = 0;
    int falseAlarms = 0;
    for (const Sighting& sighting : kSightings) {
        CAPTURE(sighting.pattern);
        CAPTURE(sighting.frame);
        const Template pattern = templateNamed(sighting.pattern);
        const Image& frame = frameNamed(sighting.frame);
        const MatchOptions options{.threshold = sighting.threshold};
        const std::optional<Match> best = findBest(frame.view(), pattern, options);
        const std::vector<Match> all = findAll(frame.view(), pattern, options);
        if (sighting.reference >= sighting.threshold) {
            const Rect expected{sighting.x, sighting.y, pattern.width(), pattern.height()};
            const bool hit = best && best->rect == expected;
            found += hit ? 1 : 0;
            missed += hit ? 0 : 1;
            REQUIRE(best);
            CHECK(best->rect == expected);
            CHECK(std::abs(best->score - sighting.reference) < kReferenceTolerance);
            REQUIRE(all.size() == 1);
            CHECK(all.front() == *best);
        } else {
            rejected += best ? 0 : 1;
            falseAlarms += best ? 1 : 0;
            CHECK_FALSE(best);
            CHECK(all.empty());
            const auto exhaustive = findBest(frame.view(), pattern, {.threshold = -1.0, .maxLevel = 0});
            REQUIRE(exhaustive);
            CHECK(exhaustive->rect.x == sighting.x);
            CHECK(exhaustive->rect.y == sighting.y);
            CHECK(std::abs(exhaustive->score - sighting.reference) < kReferenceTolerance);
        }
    }
    MESSAGE("present: " << found << " found, " << missed << " missed; absent: " << rejected << " rejected, "
                        << falseAlarms << " false alarms");
    CHECK(found == 26);
    CHECK(rejected == 22);
}

TEST_CASE("vision fixtures: the AVX2 path agrees with the scalar reference on every fixture")
{
    if (!avx2Available()) {
        MESSAGE("this CPU has no AVX2: only the scalar path runs");
        return;
    }
    for (const std::string_view name :
         {"h265_top", "av1_top", "h264_scrolled", "av1_bottom", "av1_landscape", "h264_rotating"}) {
        CAPTURE(name);
        const Image& frame = frameNamed(name);
        CHECK(pixelsOf(downsample(frame.view(), Simd::Scalar)) == pixelsOf(downsample(frame.view(), Simd::Avx2)));
    }
    for (const Sighting& sighting : kSightings) {
        CAPTURE(sighting.pattern);
        CAPTURE(sighting.frame);
        const Template pattern = templateNamed(sighting.pattern);
        const Image& frame = frameNamed(sighting.frame);
        const std::vector<Match> scalar = findAll(frame.view(), pattern, {.threshold = -1.0, .simd = Simd::Scalar});
        const std::vector<Match> fast = findAll(frame.view(), pattern, {.threshold = -1.0, .simd = Simd::Avx2});
        CHECK(!scalar.empty());
        CHECK(scalar == fast);
        const ImageView around = frame.view().crop(
            {sighting.x - 20, sighting.y - 20, pattern.width() + 40, pattern.height() + 40});
        const ScoreMap scalarMap = scoreMap(around, pattern, Simd::Scalar);
        const ScoreMap fastMap = scoreMap(around, pattern, Simd::Avx2);
        CHECK(scalarMap.width > 0);
        CHECK(scalarMap.scores == fastMap.scores);
    }
}

TEST_CASE("vision fixtures: a masked icon is found on another background, where the plain one scores lower")
{
    const Image icon = crop(fixture("template", "profile"), {3, 4, 32, 32});
    const Image mask = disc(32, 32);
    auto made = Template::make(icon.view(), mask.view());
    REQUIRE(made);
    const Template masked = std::move(*made);
    auto plainMade = Template::make(icon.view());
    REQUIRE(plainMade);
    const Template plain = std::move(*plainMade);

    const auto inOtherCodec = findBest(frameNamed("av1_top").view(), masked);
    REQUIRE(inOtherCodec);
    CHECK(inOtherCodec->rect == Rect{239, 48, 32, 32});
    CHECK(std::abs(inOtherCodec->score - 0.9028) < kReferenceTolerance);
    for (const std::string_view name : {"h264_scrolled", "av1_bottom", "av1_landscape", "h264_rotating"}) {
        CAPTURE(name);
        CHECK_FALSE(findBest(frameNamed(name).view(), masked));
    }

    Image pasted = frameNamed("av1_landscape");
    paste(pasted, icon, 22, 150, &mask);
    for (const Simd simd : {Simd::Scalar, Simd::Avx2}) {
        const auto found = findBest(pasted.view(), masked, {.simd = simd});
        REQUIRE(found);
        CHECK(found->rect == Rect{22, 150, 32, 32});
        CHECK(found->score > 0.999999);
        CHECK_FALSE(findBest(pasted.view(), plain, {.simd = simd}));
        const auto unmasked = findBest(pasted.view(), plain, {.roi = {22, 150, 32, 32}, .threshold = -1.0, .simd = simd});
        REQUIRE(unmasked);
        CHECK(std::abs(unmasked->score - 0.6482) < kReferenceTolerance);
    }
}

TEST_CASE("vision fixtures: one match per search bar on a busy 1920x1080 screen")
{
    const Image screen = screen1080();
    const Template pattern = templateNamed("search_bar");
    const std::vector<Match> found = findAll(screen.view(), pattern);
    const Rect expected[] = {{20, 130, 136, 30}, {1336, 35, 136, 30}, {660, 51, 136, 30},
                             {20, 424, 136, 30}, {1300, 339, 136, 30}};
    const double scores[] = {1.0, 0.8790, 0.8737, 0.8636, 0.8427};
    REQUIRE(found.size() == std::size(expected));
    for (std::size_t index = 0; index < found.size(); ++index) {
        CAPTURE(index);
        CHECK(found[index].rect == expected[index]);
        CHECK(std::abs(found[index].score - scores[index]) < kReferenceTolerance);
    }
}

TEST_CASE("vision fixtures: a template cut anywhere from a screen full of text is found there at threshold 0.9 (RC1 D-040)")
{
    const Image& frame = frameNamed("h265_top");
    const Rect areas[] = {{17, 371, 129, 46}, {33, 141, 129, 46}, {7, 433, 101, 31},
                          {51, 187, 61, 19},  {121, 505, 129, 46}, {3, 245, 97, 29}};
    for (const Rect& area : areas) {
        CAPTURE(area.x);
        CAPTURE(area.y);
        CAPTURE(area.width);
        CAPTURE(area.height);
        auto made = Template::make(crop(frame, area).view());
        REQUIRE(made);
        const MatchOptions options{.threshold = 0.9};
        const std::vector<Match> all = findAll(frame.view(), *made, options);
        REQUIRE_FALSE(all.empty());
        CHECK(all.front().rect == area);
        CHECK(all.front().score > 0.99);
        const std::optional<Match> best = findBest(frame.view(), *made, options);
        REQUIRE(best);
        CHECK(best->rect == area);
    }
}
