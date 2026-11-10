#include "gs/vision/Color.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <vector>

namespace gs::vision {

namespace {

struct Run {
    int row = 0;
    int start = 0;
    int end = 0;
};

struct Group {
    std::int64_t pixels = 0;
    int left = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    int right = std::numeric_limits<int>::min();
    int bottom = std::numeric_limits<int>::min();
};

std::size_t rootOf(std::vector<std::size_t>& parent, std::size_t index) noexcept
{
    while (parent[index] != index) {
        parent[index] = parent[parent[index]];
        index = parent[index];
    }
    return index;
}

void join(std::vector<std::size_t>& parent, std::size_t a, std::size_t b) noexcept
{
    const std::size_t first = rootOf(parent, a);
    const std::size_t second = rootOf(parent, b);
    if (first < second) {
        parent[second] = first;
    } else if (second < first) {
        parent[first] = second;
    }
}

}

int colorReach(double tolerance) noexcept
{
    if (!(tolerance > 0.0)) {
        return 0;
    }
    return static_cast<int>(std::floor(std::min(tolerance, 1.0) * 255.0 + 1e-9));
}

bool colorMatches(const std::uint8_t* pixel, Rgb color, int reach) noexcept
{
    return std::abs(static_cast<int>(pixel[0]) - static_cast<int>(color.red)) <= reach
        && std::abs(static_cast<int>(pixel[1]) - static_cast<int>(color.green)) <= reach
        && std::abs(static_cast<int>(pixel[2]) - static_cast<int>(color.blue)) <= reach;
}

std::optional<ColorPatch> findColor(ColorView image, Rgb color, const ColorOptions& options)
{
    if (image.empty()) {
        return std::nullopt;
    }
    const Rect roi = options.roi.empty() ? image.bounds() : intersect(options.roi, image.bounds());
    if (roi.empty()) {
        return std::nullopt;
    }
    const int reach = colorReach(options.tolerance);
    std::vector<Run> runs;
    std::vector<std::size_t> parent;
    std::size_t previousBegin = 0;
    std::size_t previousEnd = 0;
    for (int y = roi.y; y < roi.y + roi.height; ++y) {
        const std::size_t rowBegin = runs.size();
        int x = roi.x;
        while (x < roi.x + roi.width) {
            if (!colorMatches(image.at(x, y), color, reach)) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < roi.x + roi.width && colorMatches(image.at(x, y), color, reach)) {
                ++x;
            }
            runs.push_back({y, start, x});
            parent.push_back(parent.size());
        }
        std::size_t above = previousBegin;
        for (std::size_t index = rowBegin; index < runs.size(); ++index) {
            const Run& current = runs[index];
            while (above < previousEnd && runs[above].end < current.start) {
                ++above;
            }
            for (std::size_t touching = above; touching < previousEnd && runs[touching].start <= current.end;
                 ++touching) {
                join(parent, index, touching);
            }
        }
        previousBegin = rowBegin;
        previousEnd = runs.size();
    }
    if (runs.empty()) {
        return std::nullopt;
    }
    std::vector<Group> groups(runs.size());
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const Run& run = runs[index];
        Group& group = groups[rootOf(parent, index)];
        group.pixels += run.end - run.start;
        group.left = std::min(group.left, run.start);
        group.right = std::max(group.right, run.end - 1);
        group.top = std::min(group.top, run.row);
        group.bottom = std::max(group.bottom, run.row);
    }
    std::size_t best = 0;
    for (std::size_t index = 1; index < groups.size(); ++index) {
        if (groups[index].pixels > groups[best].pixels) {
            best = index;
        }
    }
    const Group& chosen = groups[best];
    const Rect rect{chosen.left, chosen.top, chosen.right - chosen.left + 1, chosen.bottom - chosen.top + 1};
    return ColorPatch{.rect = rect,
                      .pixels = chosen.pixels,
                      .score = static_cast<double>(chosen.pixels) / static_cast<double>(rect.area())};
}

}
