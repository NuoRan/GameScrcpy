#include "gs/vision/IntegralImage.h"

#include <algorithm>

namespace gs::vision {

void IntegralImage::assign(ImageView source)
{
    m_width = source.empty() ? 0 : source.width();
    m_height = source.empty() ? 0 : source.height();
    const std::size_t pitch = static_cast<std::size_t>(m_width) + 1;
    const std::size_t size = pitch * (static_cast<std::size_t>(m_height) + 1);
    m_sums.resize(size);
    m_squares.resize(size);
    std::fill_n(m_sums.begin(), pitch, 0);
    std::fill_n(m_squares.begin(), pitch, 0);
    for (int y = 0; y < m_height; ++y) {
        const std::uint8_t* pixels = source.row(y);
        const std::int64_t* sumsAbove = m_sums.data() + static_cast<std::size_t>(y) * pitch;
        const std::int64_t* squaresAbove = m_squares.data() + static_cast<std::size_t>(y) * pitch;
        std::int64_t* sums = m_sums.data() + static_cast<std::size_t>(y + 1) * pitch;
        std::int64_t* squares = m_squares.data() + static_cast<std::size_t>(y + 1) * pitch;
        sums[0] = 0;
        squares[0] = 0;
        std::int64_t rowSum = 0;
        std::int64_t rowSquares = 0;
        for (int x = 0; x < m_width; ++x) {
            const std::int64_t value = pixels[x];
            rowSum += value;
            rowSquares += value * value;
            sums[x + 1] = sumsAbove[x + 1] + rowSum;
            squares[x + 1] = squaresAbove[x + 1] + rowSquares;
        }
    }
}

}
