#pragma once

#include "gs/vision/Image.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gs::vision {

class IntegralImage
{
public:
    IntegralImage() noexcept = default;
    explicit IntegralImage(ImageView source) { assign(source); }

    void assign(ImageView source);

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

    [[nodiscard]] std::int64_t sum(const Rect& area) const noexcept { return total(m_sums, area); }
    [[nodiscard]] std::int64_t squareSum(const Rect& area) const noexcept { return total(m_squares, area); }

private:
    [[nodiscard]] std::int64_t total(const std::vector<std::int64_t>& table, const Rect& area) const noexcept
    {
        const std::ptrdiff_t pitch = m_width + 1;
        const std::ptrdiff_t top = area.y * pitch;
        const std::ptrdiff_t bottom = (area.y + area.height) * pitch;
        const std::ptrdiff_t left = area.x;
        const std::ptrdiff_t right = area.x + area.width;
        return table[bottom + right] - table[top + right] - table[bottom + left] + table[top + left];
    }

    int m_width = 0;
    int m_height = 0;
    std::vector<std::int64_t> m_sums;
    std::vector<std::int64_t> m_squares;
};

}
