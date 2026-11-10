#include "TestImages.h"

#include <gs/vision/Matcher.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace gs::vision;
using gs::vision::testing::crop;
using gs::vision::testing::scene;
using gs::vision::testing::screen1080;

namespace {

#ifdef NDEBUG
constexpr bool kOptimised = true;
constexpr int kRuns = 31;
#else
constexpr bool kOptimised = false;
constexpr int kRuns = 1;
#endif

constexpr double kWholeFrameMs = 5.0;
constexpr double kRoiMs = 1.0;

struct Timing {
    double median = 0.0;
    double fastest = 0.0;
    double slowest = 0.0;
};

Timing measure(const std::function<void()>& work)
{
    std::vector<double> samples;
    for (int run = 0; run < kRuns; ++run) {
        const auto start = std::chrono::steady_clock::now();
        work();
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::ranges::sort(samples);
    return {samples[samples.size() / 2], samples.front(), samples.back()};
}

std::string describe(const Timing& timing)
{
    return "median " + std::to_string(timing.median) + " ms, fastest " + std::to_string(timing.fastest) +
           " ms, slowest " + std::to_string(timing.slowest) + " ms";
}

std::string nameOf(Simd simd)
{
    return resolvedSimd(simd) == Simd::Avx2 ? "AVX2" : "scalar";
}

Template prepared(ImageView pixels)
{
    auto made = Template::make(pixels);
    REQUIRE(made);
    return std::move(*made);
}

}

TEST_CASE("vision benchmark: a 96x96 template on a 1920x1080 screen, whole frame and with a region of interest")
{
    const Image screen = screen1080();
    const Rect target{1320, 100, 96, 96};
    const Template pattern = prepared(screen.view().crop(target));
    const Image strangerPixels = crop(scene(200, 200, 7), {50, 50, 96, 96});
    const Template stranger = prepared(strangerPixels.view());
    const Rect nearby{1200, 40, 480, 270};
    const Rect quarter{960, 0, 960, 540};

    std::vector<Simd> simds{Simd::Scalar};
    if (avx2Available()) {
        simds.push_back(Simd::Avx2);
    }
    MESSAGE(std::string(kOptimised ? "optimised build" : "debug build: timings are printed, not checked"));
    for (const Simd simd : simds) {
        CAPTURE(nameOf(simd));
        std::optional<Match> found;
        const Timing whole = measure([&] { found = findBest(screen.view(), pattern, {.simd = simd}); });
        REQUIRE(found);
        CHECK(found->rect == target);
        CHECK(found->score > 0.999999);
        const int level = found->level;

        const Timing absent = measure([&] { found = findBest(screen.view(), stranger, {.simd = simd}); });
        CHECK_FALSE(found);

        const Timing near = measure([&] { found = findBest(screen.view(), pattern, {.roi = nearby, .simd = simd}); });
        REQUIRE(found);
        CHECK(found->rect == target);

        const Timing wide = measure([&] { found = findBest(screen.view(), pattern, {.roi = quarter, .simd = simd}); });
        REQUIRE(found);
        CHECK(found->rect == target);

        std::vector<Match> all;
        const Timing every = measure([&] { all = findAll(screen.view(), pattern, {.threshold = 0.75, .simd = simd}); });
        REQUIRE(all.size() == 2);
        CHECK(all[0].rect == target);
        CHECK(all[1].rect == Rect{4, 221, 96, 96});

        MESSAGE(nameOf(simd) << ", coarsest level " << level << "\n  whole frame, found: " << describe(whole)
                             << "\n  whole frame, absent: " << describe(absent)
                             << "\n  480x270 region: " << describe(near) << "\n  960x540 region: " << describe(wide)
                             << "\n  all matches above 0.75: " << describe(every));
        if constexpr (kOptimised) {
            if (resolvedSimd(simd) == Simd::Avx2) {
                CHECK(whole.median <= kWholeFrameMs);
                CHECK(absent.median <= kWholeFrameMs);
                CHECK(near.median <= kRoiMs);
            }
        }
    }

    if constexpr (kOptimised) {
        if (!avx2Available()) {
            return;
        }
        std::optional<Match> exhaustive;
        const auto start = std::chrono::steady_clock::now();
        exhaustive = findBest(screen.view(), pattern, {.maxLevel = 0});
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        REQUIRE(exhaustive);
        CHECK(exhaustive->rect == target);
        MESSAGE("exhaustive full-resolution search with AVX2: " << milliseconds << " ms");
    }
}

TEST_CASE("vision benchmark: whole screen, three templates one after another, and every match above 0.75")
{
    const Image screen = screen1080();
    const Rect target{1320, 100, 96, 96};
    const Template icon = prepared(screen.view().crop(target));
    const Image smallPixels = crop(scene(200, 200, 17), {40, 90, 32, 32});
    const Template small = prepared(smallPixels.view());
    const Image panelPixels = crop(screen, {640, 288, 192, 144});
    const Template panel = prepared(panelPixels.view());
    const Image strangerPixels = crop(scene(200, 200, 7), {50, 50, 96, 96});
    const Template stranger = prepared(strangerPixels.view());

    std::vector<Simd> simds{Simd::Scalar};
    if (avx2Available()) {
        simds.push_back(Simd::Avx2);
    }
    MESSAGE(std::string(kOptimised ? "optimised build" : "debug build: timings are printed, not checked"));
    for (const Simd simd : simds) {
        CAPTURE(nameOf(simd));
        std::optional<Match> found;
        const Timing absent = measure([&] { found = findBest(screen.view(), stranger, {.simd = simd}); });
        CHECK_FALSE(found);
        const Timing three = measure([&] {
            static_cast<void>(findBest(screen.view(), icon, {.simd = simd}));
            static_cast<void>(findBest(screen.view(), small, {.simd = simd}));
            static_cast<void>(findBest(screen.view(), panel, {.simd = simd}));
        });
        std::vector<Match> every;
        const Timing all =
            measure([&] { every = findAll(screen.view(), icon, {.threshold = 0.75, .simd = simd}); });
        REQUIRE(every.size() == 2);
        CHECK(every.front().rect == target);
        MESSAGE(nameOf(simd) << "\n  whole frame, absent: " << describe(absent)
                             << "\n  icon, 32x32 and 192x144 one after another on one frame: " << describe(three)
                             << "\n  every match above 0.75 (one template, whole frame): " << describe(all));
    }
}
