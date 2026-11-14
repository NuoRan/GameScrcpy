#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gs::app {

struct LanAddress {
    std::uint32_t ip = 0;
    int prefix = 0;
};

[[nodiscard]] std::vector<std::uint32_t> lanHosts(std::span<const LanAddress> own, std::size_t limit);

}
