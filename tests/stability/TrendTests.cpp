#include "stability/Trend.h"

#include <doctest/doctest.h>

#include <vector>

using gs::stability::TrendPoint;

TEST_CASE("endurance trend: the median of odd and even counts, and of nothing")
{
    CHECK(gs::stability::medianOf({3.0, 1.0, 2.0}) == 2.0);
    CHECK(gs::stability::medianOf({4.0, 1.0, 3.0, 2.0}) == 2.5);
    CHECK(gs::stability::medianOf({}) == 0.0);
}

TEST_CASE("endurance trend: the least-squares slope of a line, a flat series and a single point")
{
    std::vector<TrendPoint> line;
    std::vector<TrendPoint> flat;
    for (int i = 0; i < 10; ++i) {
        line.push_back({static_cast<double>(i), 2.0 * i + 1.0});
        flat.push_back({static_cast<double>(i), 7.0});
    }
    CHECK(gs::stability::slopeOf(line) == doctest::Approx(2.0));
    CHECK(gs::stability::slopeOf(flat) == doctest::Approx(0.0));
    CHECK(gs::stability::slopeOf(std::vector<TrendPoint>{{3.0, 5.0}}) == 0.0);
}

TEST_CASE("endurance trend: a steady leak grows between the first and the last third, a single spike does not")
{
    std::vector<TrendPoint> leaking;
    std::vector<TrendPoint> spiky;
    for (int i = 0; i < 99; ++i) {
        leaking.push_back({static_cast<double>(i), 100.0 + i});
        spiky.push_back({static_cast<double>(i), i == 70 ? 80.0 : 50.0});
    }
    const gs::stability::Trend leak = gs::stability::trendOf(leaking);
    CHECK(leak.first == 116.0);
    CHECK(leak.last == 182.0);
    CHECK(leak.growth == 66.0);
    CHECK(leak.slope == doctest::Approx(1.0));
    CHECK(leak.peak == 198.0);

    const gs::stability::Trend spike = gs::stability::trendOf(spiky);
    CHECK(spike.growth == 0.0);
    CHECK(spike.peak == 80.0);
}
