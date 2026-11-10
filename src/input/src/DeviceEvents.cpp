#include "gs/input/DeviceEvents.h"

#include "gs/input/MappingEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace gs::input {

namespace {

struct ScanName {
    std::uint16_t code;
    std::string_view name;
};

constexpr std::uint16_t kExtended = 0xE000;
constexpr std::uint16_t kPauseLead = 0x1D;
constexpr std::uint16_t kPauseTail = 0x45;
constexpr std::uint16_t kFakeShiftLeft = 0x2A;
constexpr std::uint16_t kFakeShiftRight = 0x36;
constexpr std::uint16_t kMakeCodeLimit = 0x80;

constexpr auto kScanNames = std::to_array<ScanName>({
    {0x01, "Escape"},        {0x02, "Digit1"},         {0x03, "Digit2"},         {0x04, "Digit3"},
    {0x05, "Digit4"},        {0x06, "Digit5"},         {0x07, "Digit6"},         {0x08, "Digit7"},
    {0x09, "Digit8"},        {0x0A, "Digit9"},         {0x0B, "Digit0"},         {0x0C, "Minus"},
    {0x0D, "Equal"},         {0x0E, "Backspace"},      {0x0F, "Tab"},            {0x10, "KeyQ"},
    {0x11, "KeyW"},          {0x12, "KeyE"},           {0x13, "KeyR"},           {0x14, "KeyT"},
    {0x15, "KeyY"},          {0x16, "KeyU"},           {0x17, "KeyI"},           {0x18, "KeyO"},
    {0x19, "KeyP"},          {0x1A, "BracketLeft"},    {0x1B, "BracketRight"},   {0x1C, "Enter"},
    {0x1D, "ControlLeft"},   {0x1E, "KeyA"},           {0x1F, "KeyS"},           {0x20, "KeyD"},
    {0x21, "KeyF"},          {0x22, "KeyG"},           {0x23, "KeyH"},           {0x24, "KeyJ"},
    {0x25, "KeyK"},          {0x26, "KeyL"},           {0x27, "Semicolon"},      {0x28, "Quote"},
    {0x29, "Backquote"},     {0x2A, "ShiftLeft"},      {0x2B, "Backslash"},      {0x2C, "KeyZ"},
    {0x2D, "KeyX"},          {0x2E, "KeyC"},           {0x2F, "KeyV"},           {0x30, "KeyB"},
    {0x31, "KeyN"},          {0x32, "KeyM"},           {0x33, "Comma"},          {0x34, "Period"},
    {0x35, "Slash"},         {0x36, "ShiftRight"},     {0x37, "NumpadMultiply"}, {0x38, "AltLeft"},
    {0x39, "Space"},         {0x3A, "CapsLock"},       {0x3B, "F1"},             {0x3C, "F2"},
    {0x3D, "F3"},            {0x3E, "F4"},             {0x3F, "F5"},             {0x40, "F6"},
    {0x41, "F7"},            {0x42, "F8"},             {0x43, "F9"},             {0x44, "F10"},
    {0x45, "NumLock"},       {0x46, "ScrollLock"},     {0x47, "Numpad7"},        {0x48, "Numpad8"},
    {0x49, "Numpad9"},       {0x4A, "NumpadSubtract"}, {0x4B, "Numpad4"},        {0x4C, "Numpad5"},
    {0x4D, "Numpad6"},       {0x4E, "NumpadAdd"},      {0x4F, "Numpad1"},        {0x50, "Numpad2"},
    {0x51, "Numpad3"},       {0x52, "Numpad0"},        {0x53, "NumpadDecimal"},  {0x54, "PrintScreen"},
    {0x56, "IntlBackslash"}, {0x57, "F11"},            {0x58, "F12"},            {0x64, "F13"},
    {0x65, "F14"},           {0x66, "F15"},            {0x67, "F16"},            {0x68, "F17"},
    {0x69, "F18"},           {0x6A, "F19"},            {0x6B, "F20"},            {0x6C, "F21"},
    {0x6D, "F22"},           {0x6E, "F23"},            {0x76, "F24"},            {0xE01C, "NumpadEnter"},
    {0xE01D, "ControlRight"}, {0xE035, "NumpadDivide"}, {0xE037, "PrintScreen"}, {0xE038, "AltRight"},
    {0xE045, "NumLock"},     {0xE046, "Pause"},        {0xE047, "Home"},         {0xE048, "ArrowUp"},
    {0xE049, "PageUp"},      {0xE04B, "ArrowLeft"},    {0xE04D, "ArrowRight"},   {0xE04F, "End"},
    {0xE050, "ArrowDown"},   {0xE051, "PageDown"},     {0xE052, "Insert"},       {0xE053, "Delete"},
    {0xE05B, "MetaLeft"},    {0xE05C, "MetaRight"},    {0xE05D, "ContextMenu"},
});

using ScanTable = std::array<std::optional<InputSource>, 2 * kMakeCodeLimit>;

std::size_t tableIndex(std::uint16_t code) noexcept
{
    const std::size_t low = code & (kMakeCodeLimit - 1);
    return (code & kExtended) == kExtended ? kMakeCodeLimit + low : low;
}

const ScanTable& scanTable()
{
    static const ScanTable table = [] {
        ScanTable built{};
        for (const ScanName& entry : kScanNames) {
            built[tableIndex(entry.code)] = parseSource(entry.name);
        }
        return built;
    }();
    return table;
}

InputSource pauseSource()
{
    static const InputSource pause = *parseSource("Pause");
    return pause;
}

constexpr InputSource mouse(std::uint16_t code) noexcept
{
    return {Device::Mouse, code};
}

constexpr InputSource gamepad(std::uint16_t code) noexcept
{
    return {Device::Gamepad, code};
}

constexpr std::uint16_t kMouseWheelUp = 5;
constexpr std::uint16_t kMouseWheelDown = 6;
constexpr std::uint16_t kMouseWheelLeft = 7;
constexpr std::uint16_t kMouseWheelRight = 8;
constexpr std::uint16_t kMouseMove = 9;

struct MouseButton {
    std::uint16_t down;
    std::uint16_t up;
    std::uint16_t code;
};

constexpr std::array<MouseButton, 5> kMouseButtons{{
    {rawmouse::kLeftDown, rawmouse::kLeftUp, 0},
    {rawmouse::kRightDown, rawmouse::kRightUp, 1},
    {rawmouse::kMiddleDown, rawmouse::kMiddleUp, 2},
    {rawmouse::kX1Down, rawmouse::kX1Up, 3},
    {rawmouse::kX2Down, rawmouse::kX2Up, 4},
}};

void pulse(MouseDecoder::Events& events, int& accumulated, int delta, std::uint16_t positive, std::uint16_t negative)
{
    if ((accumulated > 0 && delta < 0) || (accumulated < 0 && delta > 0)) {
        accumulated = 0;
    }
    accumulated += delta;
    int notches = accumulated / rawmouse::kWheelStep;
    accumulated -= notches * rawmouse::kWheelStep;
    notches = std::clamp(notches, -rawmouse::kMostNotchesPerReport, rawmouse::kMostNotchesPerReport);
    const InputSource source = mouse(notches > 0 ? positive : negative);
    for (int i = 0; i < std::abs(notches); ++i) {
        events.push_back({source, DeviceAction::Down, {}});
        events.push_back({source, DeviceAction::Up, {}});
    }
}

struct PadButton {
    std::uint16_t bit;
    std::uint16_t code;
};

constexpr std::array<PadButton, 14> kPadButtons{{
    {xinput::kA, 0},
    {xinput::kB, 1},
    {xinput::kX, 2},
    {xinput::kY, 3},
    {xinput::kLeftShoulder, 4},
    {xinput::kRightShoulder, 5},
    {xinput::kLeftThumb, 6},
    {xinput::kRightThumb, 7},
    {xinput::kStart, 8},
    {xinput::kBack, 9},
    {xinput::kDpadUp, 10},
    {xinput::kDpadDown, 11},
    {xinput::kDpadLeft, 12},
    {xinput::kDpadRight, 13},
}};

constexpr std::uint16_t kPadLeftTrigger = 14;
constexpr std::uint16_t kPadRightTrigger = 15;
constexpr std::uint16_t kPadLeftStick = 16;
constexpr std::uint16_t kPadRightStick = 17;
constexpr double kStickRange = 32767.0;
constexpr double kStickDeadzone = 0.08;

Vec2 stick(std::int16_t x, std::int16_t y) noexcept
{
    const Vec2 raw{std::clamp(x / kStickRange, -1.0, 1.0), -std::clamp(y / kStickRange, -1.0, 1.0)};
    const double length = std::hypot(raw.x, raw.y);
    if (length <= kStickDeadzone) {
        return {};
    }
    const double scaled = std::min(1.0, (length - kStickDeadzone) / (1.0 - kStickDeadzone));
    return {raw.x / length * scaled, raw.y / length * scaled};
}

void triggerEdge(GamepadTracker::Events& events, bool& pressed, std::uint8_t value, std::uint16_t code)
{
    const bool held = pressed ? value >= xinput::kTriggerRelease : value > xinput::kTriggerPress;
    if (held != pressed) {
        pressed = held;
        events.push_back({gamepad(code), held ? DeviceAction::Down : DeviceAction::Up, {}});
    }
}

}

std::optional<std::uint16_t> scanCodeOf(InputSource source)
{
    const ScanTable& table = scanTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i] == source) {
            const auto low = static_cast<std::uint16_t>(i & (kMakeCodeLimit - 1));
            return i < kMakeCodeLimit ? low : static_cast<std::uint16_t>(kExtended | low);
        }
    }
    return std::nullopt;
}

void deliver(const DeviceEvent& event, MappingEngine& engine, Microseconds now)
{
    switch (event.action) {
    case DeviceAction::Down:
        engine.buttonDown(event.source, now);
        break;
    case DeviceAction::Up:
        engine.buttonUp(event.source, now);
        break;
    case DeviceAction::Motion:
        engine.motion(event.source, event.value.x, event.value.y, now);
        break;
    case DeviceAction::Axis:
        engine.axis(event.source, event.value, now);
        break;
    }
}

std::optional<DeviceEvent> KeyboardDecoder::decode(std::uint16_t makeCode, bool e0, bool e1, bool released)
{
    const DeviceAction action = released ? DeviceAction::Up : DeviceAction::Down;
    if (e1) {
        m_pauseTail = makeCode == kPauseLead;
        if (!m_pauseTail) {
            return std::nullopt;
        }
        return DeviceEvent{pauseSource(), action, {}};
    }
    if (std::exchange(m_pauseTail, false) && !e0 && makeCode == kPauseTail) {
        return std::nullopt;
    }
    if (makeCode >= kMakeCodeLimit || (e0 && (makeCode == kFakeShiftLeft || makeCode == kFakeShiftRight))) {
        return std::nullopt;
    }
    const std::optional<InputSource>& source =
        scanTable()[tableIndex(static_cast<std::uint16_t>(makeCode | (e0 ? kExtended : 0)))];
    if (!source) {
        return std::nullopt;
    }
    return DeviceEvent{*source, action, {}};
}

MouseDecoder::Events MouseDecoder::decode(const MouseReport& report)
{
    Events events;
    if (report.dx != 0 || report.dy != 0) {
        events.push_back(
            {mouse(kMouseMove), DeviceAction::Motion, {static_cast<double>(report.dx), static_cast<double>(report.dy)}});
    }
    for (const MouseButton& button : kMouseButtons) {
        if ((report.buttonFlags & button.down) != 0) {
            events.push_back({mouse(button.code), DeviceAction::Down, {}});
        }
        if ((report.buttonFlags & button.up) != 0) {
            events.push_back({mouse(button.code), DeviceAction::Up, {}});
        }
    }
    if ((report.buttonFlags & rawmouse::kWheel) != 0) {
        pulse(events, m_wheel, report.buttonData, kMouseWheelUp, kMouseWheelDown);
    } else if ((report.buttonFlags & rawmouse::kHorizontalWheel) != 0) {
        pulse(events, m_horizontalWheel, report.buttonData, kMouseWheelRight, kMouseWheelLeft);
    }
    return events;
}

GamepadTracker::Events GamepadTracker::update(const GamepadState& next)
{
    Events events;
    const GamepadState target = next.connected ? next : GamepadState{};
    const std::uint16_t changed = static_cast<std::uint16_t>(m_state.buttons ^ target.buttons);
    for (const PadButton& button : kPadButtons) {
        if ((changed & button.bit) != 0) {
            events.push_back(
                {gamepad(button.code), (target.buttons & button.bit) != 0 ? DeviceAction::Down : DeviceAction::Up, {}});
        }
    }
    triggerEdge(events, m_leftTrigger, target.leftTrigger, kPadLeftTrigger);
    triggerEdge(events, m_rightTrigger, target.rightTrigger, kPadRightTrigger);
    if (const Vec2 left = stick(target.leftX, target.leftY); left != m_left) {
        m_left = left;
        events.push_back({gamepad(kPadLeftStick), DeviceAction::Axis, left});
    }
    if (const Vec2 right = stick(target.rightX, target.rightY); right != m_right) {
        m_right = right;
        events.push_back({gamepad(kPadRightStick), DeviceAction::Axis, right});
    }
    m_state = target;
    return events;
}

}
