#pragma once

#include <array>

struct AVFrame;

namespace gs::media {

struct ColorMatrix {
    std::array<std::array<float, 4>, 3> rows{};
};

[[nodiscard]] ColorMatrix yuvToRgb(const AVFrame& frame) noexcept;

}
