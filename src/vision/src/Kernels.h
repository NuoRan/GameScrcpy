#pragma once

#include "gs/vision/Simd.h"

#include <cstddef>
#include <cstdint>

namespace gs::vision::detail {

struct Correlation {
    const std::int16_t* image = nullptr;
    std::ptrdiff_t imageStride = 0;
    const std::int16_t* pattern = nullptr;
    int patternWidth = 0;
    int patternStride = 0;
    int patternHeight = 0;
    int columns = 0;
    int rows = 0;
    std::int64_t* sums = nullptr;
    std::ptrdiff_t sumStride = 0;
};

struct ColumnWindow {
    const std::uint8_t* pixels = nullptr;
    std::ptrdiff_t stride = 0;
    int columns = 0;
    int rows = 0;
    std::uint32_t* sums = nullptr;
    std::uint32_t* squares = nullptr;
};

inline constexpr int kPatternAlignment = 16;
inline constexpr int kPositionGroup = 4;

[[nodiscard]] constexpr int paddedWidth(int width) noexcept
{
    return (width + kPatternAlignment - 1) / kPatternAlignment * kPatternAlignment;
}

[[nodiscard]] constexpr std::ptrdiff_t planeStride(int columns, int paddedPatternWidth) noexcept
{
    return static_cast<std::ptrdiff_t>((columns + kPositionGroup - 1) / kPositionGroup * kPositionGroup) +
           paddedPatternWidth;
}

struct Kernels {
    void (*halve)(const std::uint8_t* top, const std::uint8_t* bottom, std::uint8_t* out, int width) = nullptr;
    void (*correlate)(const Correlation& job) = nullptr;
    void (*accumulateColumns)(const ColumnWindow& job) = nullptr;
};

[[nodiscard]] const Kernels& kernels(Simd simd) noexcept;

namespace scalar {

void halve(const std::uint8_t* top, const std::uint8_t* bottom, std::uint8_t* out, int width) noexcept;
void correlate(const Correlation& job) noexcept;
void accumulateColumns(const ColumnWindow& job) noexcept;

}

namespace avx2 {

void halve(const std::uint8_t* top, const std::uint8_t* bottom, std::uint8_t* out, int width) noexcept;
void correlate(const Correlation& job) noexcept;
void accumulateColumns(const ColumnWindow& job) noexcept;

}

}
