#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace gs::espflash {

using Md5Digest = std::array<std::uint8_t, 16>;

[[nodiscard]] Md5Digest md5(std::span<const std::uint8_t> data);
[[nodiscard]] std::string md5Hex(std::span<const std::uint8_t> data);

}
