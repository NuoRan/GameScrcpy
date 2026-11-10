#include "gs/vision/Glance.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

namespace gs::vision {

namespace {

int scaledSide(int side, int longer)
{
    const int target = std::min(longer, kGlanceSide);
    return std::max(1, static_cast<int>(static_cast<std::int64_t>(side) * target / longer));
}

int edge(int index, int cells, int size)
{
    return static_cast<int>(static_cast<std::int64_t>(index) * size / cells);
}

}

Glance glanceOf(ImageView source)
{
    Glance glance;
    if (source.empty()) {
        return glance;
    }
    const int longer = std::max(source.width(), source.height());
    glance.width = scaledSide(source.width(), longer);
    glance.height = scaledSide(source.height(), longer);
    glance.luma.resize(static_cast<std::size_t>(glance.width) * static_cast<std::size_t>(glance.height));
    for (int y = 0; y < glance.height; ++y) {
        const int top = edge(y, glance.height, source.height());
        const int bottom = std::max(top + 1, edge(y + 1, glance.height, source.height()));
        for (int x = 0; x < glance.width; ++x) {
            const int left = edge(x, glance.width, source.width());
            const int right = std::max(left + 1, edge(x + 1, glance.width, source.width()));
            std::uint64_t sum = 0;
            for (int row = top; row < bottom; ++row) {
                const std::uint8_t* line = source.row(row);
                for (int column = left; column < right; ++column) {
                    sum += line[column];
                }
            }
            const auto count = static_cast<std::uint64_t>(bottom - top) * static_cast<std::uint64_t>(right - left);
            glance.luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(glance.width) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>((sum + count / 2) / count);
        }
    }
    return glance;
}

double changedShare(const Glance& before, const Glance& after) noexcept
{
    if (before.empty() || before.width != after.width || before.height != after.height
        || before.luma.size() != after.luma.size()) {
        return 1.0;
    }
    std::size_t changed = 0;
    for (std::size_t at = 0; at < before.luma.size(); ++at) {
        if (std::abs(static_cast<int>(before.luma[at]) - static_cast<int>(after.luma[at])) > kGlanceStep) {
            ++changed;
        }
    }
    return static_cast<double>(changed) / static_cast<double>(before.luma.size());
}

}
