#pragma once

#include <gs/base/Clock.h>
#include <gs/base/InplaceVector.h>
#include <gs/input/Geometry.h>
#include <gs/input/InputSource.h>

#include <cstdint>
#include <optional>

namespace gs::input {

class MappingEngine;

enum class DeviceAction : std::uint8_t { Down, Up, Motion, Axis };

struct DeviceEvent {
    InputSource source;
    DeviceAction action = DeviceAction::Down;
    Vec2 value;

    friend bool operator==(const DeviceEvent&, const DeviceEvent&) = default;
};

void deliver(const DeviceEvent& event, MappingEngine& engine, Microseconds now);

[[nodiscard]] std::optional<std::uint16_t> scanCodeOf(InputSource source);

class KeyboardDecoder
{
public:
    [[nodiscard]] std::optional<DeviceEvent> decode(std::uint16_t makeCode, bool e0, bool e1, bool released);

private:
    bool m_pauseTail = false;
};

namespace rawmouse {
inline constexpr std::uint16_t kLeftDown = 0x0001;
inline constexpr std::uint16_t kLeftUp = 0x0002;
inline constexpr std::uint16_t kRightDown = 0x0004;
inline constexpr std::uint16_t kRightUp = 0x0008;
inline constexpr std::uint16_t kMiddleDown = 0x0010;
inline constexpr std::uint16_t kMiddleUp = 0x0020;
inline constexpr std::uint16_t kX1Down = 0x0040;
inline constexpr std::uint16_t kX1Up = 0x0080;
inline constexpr std::uint16_t kX2Down = 0x0100;
inline constexpr std::uint16_t kX2Up = 0x0200;
inline constexpr std::uint16_t kWheel = 0x0400;
inline constexpr std::uint16_t kHorizontalWheel = 0x0800;
inline constexpr int kWheelStep = 120;
inline constexpr int kMostNotchesPerReport = 8;
}

struct MouseReport {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::uint16_t buttonFlags = 0;
    std::int16_t buttonData = 0;
};

class MouseDecoder
{
public:
    using Events = InplaceVector<DeviceEvent, 32>;

    [[nodiscard]] Events decode(const MouseReport& report);

private:
    int m_wheel = 0;
    int m_horizontalWheel = 0;
};

namespace xinput {
inline constexpr std::uint16_t kDpadUp = 0x0001;
inline constexpr std::uint16_t kDpadDown = 0x0002;
inline constexpr std::uint16_t kDpadLeft = 0x0004;
inline constexpr std::uint16_t kDpadRight = 0x0008;
inline constexpr std::uint16_t kStart = 0x0010;
inline constexpr std::uint16_t kBack = 0x0020;
inline constexpr std::uint16_t kLeftThumb = 0x0040;
inline constexpr std::uint16_t kRightThumb = 0x0080;
inline constexpr std::uint16_t kLeftShoulder = 0x0100;
inline constexpr std::uint16_t kRightShoulder = 0x0200;
inline constexpr std::uint16_t kA = 0x1000;
inline constexpr std::uint16_t kB = 0x2000;
inline constexpr std::uint16_t kX = 0x4000;
inline constexpr std::uint16_t kY = 0x8000;
inline constexpr std::uint8_t kTriggerPress = 30;
inline constexpr std::uint8_t kTriggerRelease = 20;
}

struct GamepadState {
    bool connected = false;
    std::uint16_t buttons = 0;
    std::uint8_t leftTrigger = 0;
    std::uint8_t rightTrigger = 0;
    std::int16_t leftX = 0;
    std::int16_t leftY = 0;
    std::int16_t rightX = 0;
    std::int16_t rightY = 0;

    friend bool operator==(const GamepadState&, const GamepadState&) = default;
};

class GamepadTracker
{
public:
    using Events = InplaceVector<DeviceEvent, kGamepadSourceCount>;

    [[nodiscard]] Events update(const GamepadState& next);

private:
    GamepadState m_state;
    bool m_leftTrigger = false;
    bool m_rightTrigger = false;
    Vec2 m_left;
    Vec2 m_right;
};

}
