#pragma once

#include <span>
#include <vector>

namespace gs::stability {

struct TrendPoint {
    double x = 0.0;
    double y = 0.0;
};

struct Trend {
    double first = 0.0;
    double last = 0.0;
    double growth = 0.0;
    double slope = 0.0;
    double peak = 0.0;
};

[[nodiscard]] double medianOf(std::vector<double> values);
[[nodiscard]] double slopeOf(std::span<const TrendPoint> points);
[[nodiscard]] Trend trendOf(std::span<const TrendPoint> points);

}
