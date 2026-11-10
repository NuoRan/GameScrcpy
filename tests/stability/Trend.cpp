#include "Trend.h"

#include <algorithm>
#include <cstddef>

namespace gs::stability {

namespace {

std::vector<double> valuesOf(std::span<const TrendPoint> points)
{
    std::vector<double> values;
    values.reserve(points.size());
    for (const TrendPoint& point : points) {
        values.push_back(point.y);
    }
    return values;
}

}

double medianOf(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

double slopeOf(std::span<const TrendPoint> points)
{
    if (points.size() < 2) {
        return 0.0;
    }
    double meanX = 0.0;
    double meanY = 0.0;
    for (const TrendPoint& point : points) {
        meanX += point.x;
        meanY += point.y;
    }
    meanX /= static_cast<double>(points.size());
    meanY /= static_cast<double>(points.size());
    double spread = 0.0;
    double together = 0.0;
    for (const TrendPoint& point : points) {
        spread += (point.x - meanX) * (point.x - meanX);
        together += (point.x - meanX) * (point.y - meanY);
    }
    return spread > 0.0 ? together / spread : 0.0;
}

Trend trendOf(std::span<const TrendPoint> points)
{
    if (points.empty()) {
        return {};
    }
    const std::size_t third = std::max<std::size_t>(1, points.size() / 3);
    Trend trend;
    trend.first = medianOf(valuesOf(points.first(third)));
    trend.last = medianOf(valuesOf(points.last(third)));
    trend.growth = trend.last - trend.first;
    trend.slope = slopeOf(points);
    trend.peak = std::ranges::max(valuesOf(points));
    return trend;
}

}
