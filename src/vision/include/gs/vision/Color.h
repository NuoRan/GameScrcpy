#pragma once

#include "gs/vision/Image.h"

#include <cstdint>
#include <optional>

namespace gs::vision {

struct Rgb {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

struct ColorOptions {
    Rect roi;
    double tolerance = 0.1;
};

struct ColorPatch {
    Rect rect;
    std::int64_t pixels = 0;
    double score = 0.0;

    friend bool operator==(const ColorPatch&, const ColorPatch&) = default;
};

[[nodiscard]] int colorReach(double tolerance) noexcept;

[[nodiscard]] bool colorMatches(const std::uint8_t* pixel, Rgb color, int reach) noexcept;

[[nodiscard]] std::optional<ColorPatch> findColor(ColorView image, Rgb color, const ColorOptions& options = {});

}
