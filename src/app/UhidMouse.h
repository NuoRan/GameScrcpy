#pragma once

#include <gs/protocol/Messages.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gs::app {

inline constexpr std::uint16_t kUhidMouseId = 2;
inline constexpr std::uint16_t kUhidMouseVendor = 0x1209;
inline constexpr std::uint16_t kUhidMouseProduct = 0x0002;
inline constexpr std::string_view kUhidMouseName = "GameScrcpy Mouse";
inline constexpr std::size_t kUhidMouseReportSize = 7;

[[nodiscard]] protocol::UhidCreate uhidMouseCreate();
[[nodiscard]] protocol::UhidInput uhidMouseReport(std::uint8_t buttons, int dx, int dy, int vertical, int horizontal);

}
