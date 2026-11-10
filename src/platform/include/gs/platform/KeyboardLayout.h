#pragma once

#include <cstdint>
#include <optional>

namespace gs::platform {

[[nodiscard]] std::optional<char32_t> layoutCharacter(std::uint16_t scanCode) noexcept;

}
