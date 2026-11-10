#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gs::input {

enum class PadFamily : std::uint8_t { Generic, Xbox, DualShock4, DualSense };

inline constexpr std::uint8_t kPadBusUsb = 0x03;
inline constexpr std::uint8_t kPadBusBluetooth = 0x05;
inline constexpr std::uint8_t kPadHatCentred = 8;

namespace pad_button {
inline constexpr std::uint32_t kSouth = 1U << 0;
inline constexpr std::uint32_t kEast = 1U << 1;
inline constexpr std::uint32_t kWest = 1U << 3;
inline constexpr std::uint32_t kNorth = 1U << 4;
inline constexpr std::uint32_t kLeftShoulder = 1U << 6;
inline constexpr std::uint32_t kRightShoulder = 1U << 7;
inline constexpr std::uint32_t kLeftTrigger = 1U << 8;
inline constexpr std::uint32_t kRightTrigger = 1U << 9;
inline constexpr std::uint32_t kBack = 1U << 10;
inline constexpr std::uint32_t kStart = 1U << 11;
inline constexpr std::uint32_t kGuide = 1U << 12;
inline constexpr std::uint32_t kLeftStick = 1U << 13;
inline constexpr std::uint32_t kRightStick = 1U << 14;
inline constexpr std::uint32_t kMisc = 1U << 15;
inline constexpr std::uint32_t kTouchpad = 1U << 16;
}

struct PadPassState {
    std::uint32_t buttons = 0;
    std::uint8_t hat = kPadHatCentred;
    std::int16_t leftX = 0;
    std::int16_t leftY = 0;
    std::int16_t rightX = 0;
    std::int16_t rightY = 0;
    std::uint8_t leftTrigger = 0;
    std::uint8_t rightTrigger = 0;

    friend bool operator==(const PadPassState&, const PadPassState&) = default;
};

struct PadFeature {
    std::uint8_t reportNumber = 0;
    std::span<const std::uint8_t> data;
};

struct PadModel {
    PadFamily family = PadFamily::Generic;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint8_t bus = kPadBusUsb;
    std::string_view name;
    std::span<const std::uint8_t> descriptor;
    std::span<const PadFeature> features;
    std::uint8_t inputReport = 0;
    std::size_t inputSize = 0;
    bool rumble = false;
};

struct PadRumble {
    std::uint16_t strong = 0;
    std::uint16_t weak = 0;
    std::uint16_t leftTrigger = 0;
    std::uint16_t rightTrigger = 0;

    friend bool operator==(const PadRumble&, const PadRumble&) = default;
};

[[nodiscard]] const PadModel& padModel(PadFamily family) noexcept;
[[nodiscard]] std::vector<std::uint8_t> padInputReport(PadFamily family, const PadPassState& state);
[[nodiscard]] std::optional<PadRumble> padRumble(PadFamily family, std::span<const std::uint8_t> output) noexcept;

}
