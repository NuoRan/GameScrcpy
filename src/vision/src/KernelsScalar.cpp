#include "Kernels.h"

namespace gs::vision::detail::scalar {

void halve(const std::uint8_t* top, const std::uint8_t* bottom, std::uint8_t* out, int width) noexcept
{
    for (int x = 0; x < width; ++x) {
        const int sum = top[2 * x] + top[2 * x + 1] + bottom[2 * x] + bottom[2 * x + 1];
        out[x] = static_cast<std::uint8_t>((sum + 2) >> 2);
    }
}

void correlate(const Correlation& job) noexcept
{
    for (int y = 0; y < job.rows; ++y) {
        std::int64_t* sums = job.sums + y * job.sumStride;
        for (int x = 0; x < job.columns; ++x) {
            std::int64_t total = 0;
            for (int j = 0; j < job.patternHeight; ++j) {
                const std::int16_t* image = job.image + (y + j) * job.imageStride + x;
                const std::int16_t* pattern = job.pattern + static_cast<std::ptrdiff_t>(j) * job.patternStride;
                std::int32_t row = 0;
                for (int i = 0; i < job.patternWidth; ++i) {
                    row += image[i] * pattern[i];
                }
                total += row;
            }
            sums[x] = total;
        }
    }
}

void accumulateColumns(const ColumnWindow& job) noexcept
{
    for (int c = 0; c < job.columns; ++c) {
        job.sums[c] = 0;
        job.squares[c] = 0;
    }
    for (int r = 0; r < job.rows; ++r) {
        const std::uint8_t* row = job.pixels + r * job.stride;
        for (int c = 0; c < job.columns; ++c) {
            const std::uint32_t value = row[c];
            job.sums[c] += value;
            job.squares[c] += value * value;
        }
    }
}

}
