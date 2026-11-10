#pragma once

#include <array>
#include <cstdint>

namespace gs::app {

inline constexpr std::array<std::int32_t, 4> kSponsorMilestones{10, 50, 100, 200};
inline constexpr char kSponsorUrl[] = "https://afdian.com/a/GameScrcpy?utm_source=copylink&utm_medium=link";

[[nodiscard]] constexpr std::int32_t sponsorMilestoneDue(std::int32_t launches, std::int32_t asked) noexcept
{
    std::int32_t due = 0;
    for (const std::int32_t milestone : kSponsorMilestones) {
        if (milestone > asked && milestone <= launches) {
            due = milestone;
        }
    }
    return due;
}

}
