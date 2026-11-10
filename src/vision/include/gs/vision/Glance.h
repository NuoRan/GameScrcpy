#pragma once

#include "gs/vision/Image.h"

#include <cstdint>
#include <vector>

namespace gs::vision {

inline constexpr int kGlanceSide = 64;
inline constexpr int kGlanceStep = 24;

struct Glance {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;

    [[nodiscard]] bool empty() const noexcept { return luma.empty(); }

    friend bool operator==(const Glance&, const Glance&) = default;
};

[[nodiscard]] Glance glanceOf(ImageView source);
[[nodiscard]] double changedShare(const Glance& before, const Glance& after) noexcept;

}
