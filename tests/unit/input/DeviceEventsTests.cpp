#include "gs/input/DeviceEvents.h"
#include "gs/input/MappingEngine.h"

#include <doctest/doctest.h>

#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace gs;
using namespace gs::input;

namespace {

InputSource source(std::string_view name)
{
    const auto parsed = parseSource(name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

DeviceEvent down(std::string_view name)
{
    return {source(name), DeviceAction::Down, {}};
}

DeviceEvent up(std::string_view name)
{
    return {source(name), DeviceAction::Up, {}};
}

std::string nameOf(const std::optional<DeviceEvent>& event)
{
    return event ? sourceName(event->source) : std::string("-");
}

std::string keyName(std::uint16_t makeCode, bool e0 = false)
{
    KeyboardDecoder decoder;
    return nameOf(decoder.decode(makeCode, e0, false, false));
}

template <typename Events>
std::vector<DeviceEvent> listOf(const Events& events)
{
    return {events.begin(), events.end()};
}

class Touches final : public TouchFrameSink
{
public:
    bool trySend(const protocol::TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }

    std::vector<protocol::TouchFrame> frames;
};

class Listener final : public EngineListener
{
public:
    void pointerState(PointerState) override {}
    void notice(EngineNotice) override {}
    void liveChanged(const std::vector<bool>&) override {}
};

}

TEST_CASE("every keyboard source is reachable through its Windows scan code (ADR-0069)")
{
    std::set<InputSource> seen;
    for (std::uint16_t code = 0; code < 0x80; ++code) {
        for (const bool e0 : {false, true}) {
            KeyboardDecoder decoder;
            if (const auto event = decoder.decode(code, e0, false, false)) {
                CHECK(event->action == DeviceAction::Down);
                seen.insert(event->source);
            }
        }
    }
    KeyboardDecoder decoder;
    if (const auto pause = decoder.decode(0x1D, false, true, false)) {
        seen.insert(pause->source);
    }
    CHECK(seen.size() == kKeyboardSourceCount);

    CHECK(keyName(0x11) == "KeyW");
    CHECK(keyName(0x1E) == "KeyA");
    CHECK(keyName(0x0B) == "Digit0");
    CHECK(keyName(0x29) == "Backquote");
    CHECK(keyName(0x56) == "IntlBackslash");
    CHECK(keyName(0x57) == "F11");
    CHECK(keyName(0x76) == "F24");
    CHECK(keyName(0x48) == "Numpad8");
    CHECK(keyName(0x48, true) == "ArrowUp");
    CHECK(keyName(0x1D) == "ControlLeft");
    CHECK(keyName(0x1D, true) == "ControlRight");
    CHECK(keyName(0x38, true) == "AltRight");
    CHECK(keyName(0x1C, true) == "NumpadEnter");
    CHECK(keyName(0x5B, true) == "MetaLeft");
    CHECK(keyName(0x5D, true) == "ContextMenu");
}

TEST_CASE("keyboard sources lead back to the scan code the decoder reads (INP-013)")
{
    CHECK(scanCodeOf(source("KeyW")) == std::uint16_t{0x11});
    CHECK(scanCodeOf(source("Backquote")) == std::uint16_t{0x29});
    CHECK(scanCodeOf(source("ArrowUp")) == std::uint16_t{0xE048});
    CHECK(scanCodeOf(source("AltRight")) == std::uint16_t{0xE038});
    CHECK_FALSE(scanCodeOf(source("Mouse.Left")).has_value());
    CHECK_FALSE(scanCodeOf(source("Gamepad.A")).has_value());
    for (std::uint16_t code = 1; code < 0x80; ++code) {
        for (const bool extended : {false, true}) {
            KeyboardDecoder decoder;
            const std::optional<DeviceEvent> event = decoder.decode(code, extended, false, false);
            if (!event || keyName(code, extended) == "Pause" || keyName(code, extended) == "PrintScreen" ||
                keyName(code, extended) == "NumLock") {
                continue;
            }
            CAPTURE(code);
            CAPTURE(extended);
            CHECK(scanCodeOf(event->source) == static_cast<std::uint16_t>(extended ? 0xE000 | code : code));
        }
    }
}

TEST_CASE("Pause is one key, fake shifts and unknown codes are dropped, special codes map (ADR-0069)")
{
    KeyboardDecoder decoder;
    CHECK(nameOf(decoder.decode(0x1D, false, true, false)) == "Pause");
    CHECK_FALSE(decoder.decode(0x45, false, false, false).has_value());
    const auto release = decoder.decode(0x1D, false, true, true);
    REQUIRE(release.has_value());
    CHECK(*release == up("Pause"));
    CHECK_FALSE(decoder.decode(0x45, false, false, true).has_value());
    CHECK(nameOf(decoder.decode(0x45, false, false, false)) == "NumLock");
    CHECK(keyName(0x45, true) == "NumLock");

    CHECK_FALSE(decoder.decode(0x2A, true, false, false).has_value());
    CHECK_FALSE(decoder.decode(0x36, true, false, true).has_value());
    CHECK(keyName(0x2A) == "ShiftLeft");
    CHECK(keyName(0x46, true) == "Pause");
    CHECK(keyName(0x54) == "PrintScreen");
    CHECK(keyName(0x37, true) == "PrintScreen");
    CHECK(keyName(0x37) == "NumpadMultiply");

    CHECK_FALSE(decoder.decode(0x00, false, false, false).has_value());
    CHECK_FALSE(decoder.decode(0x55, false, false, false).has_value());
    CHECK_FALSE(decoder.decode(0xFF, false, false, false).has_value());
    CHECK_FALSE(decoder.decode(0x2A, false, true, false).has_value());
    CHECK(decoder.decode(0x11, false, false, true) == up("KeyW"));
}

TEST_CASE("mouse reports become motion, button edges and one pulse per wheel notch (ADR-0069)")
{
    MouseDecoder decoder;
    CHECK(listOf(decoder.decode({.dx = 5, .dy = -3, .buttonFlags = rawmouse::kLeftDown})) ==
          std::vector<DeviceEvent>{{source("Mouse.Move"), DeviceAction::Motion, {5.0, -3.0}}, down("Mouse.Left")});
    CHECK(listOf(decoder.decode({.buttonFlags = rawmouse::kRightDown | rawmouse::kRightUp})) ==
          std::vector<DeviceEvent>{down("Mouse.Right"), up("Mouse.Right")});
    CHECK(listOf(decoder.decode({.buttonFlags = rawmouse::kMiddleUp | rawmouse::kX1Down | rawmouse::kX2Up})) ==
          std::vector<DeviceEvent>{up("Mouse.Middle"), down("Mouse.X1"), up("Mouse.X2")});
    CHECK(decoder.decode({}).empty());

    CHECK(listOf(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = 120})) ==
          std::vector<DeviceEvent>{down("Mouse.WheelUp"), up("Mouse.WheelUp")});
    CHECK(listOf(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = -240})) ==
          std::vector<DeviceEvent>{down("Mouse.WheelDown"), up("Mouse.WheelDown"), down("Mouse.WheelDown"),
                                   up("Mouse.WheelDown")});
    CHECK(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = 40}).empty());
    CHECK(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = 40}).empty());
    CHECK(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = 40}).size() == 2);
    CHECK(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = 100}).empty());
    CHECK(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = -100}).empty());
    CHECK(decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = -20}).size() == 2);

    const auto burst = decoder.decode({.buttonFlags = rawmouse::kWheel, .buttonData = 120 * 20});
    CHECK(burst.size() == 2 * static_cast<std::size_t>(rawmouse::kMostNotchesPerReport));
    CHECK(listOf(decoder.decode({.buttonFlags = rawmouse::kHorizontalWheel, .buttonData = 120})) ==
          std::vector<DeviceEvent>{down("Mouse.WheelRight"), up("Mouse.WheelRight")});
    CHECK(listOf(decoder.decode({.buttonFlags = rawmouse::kHorizontalWheel, .buttonData = -120})) ==
          std::vector<DeviceEvent>{down("Mouse.WheelLeft"), up("Mouse.WheelLeft")});
}

TEST_CASE("gamepad states become button edges, trigger presses with hysteresis and normalized sticks (ADR-0069)")
{
    GamepadTracker tracker;
    CHECK(tracker.update({}).empty());
    CHECK(listOf(tracker.update({.connected = true, .buttons = xinput::kA, .leftX = 32767, .leftY = 0})) ==
          std::vector<DeviceEvent>{down("Gamepad.A"), {source("Gamepad.LeftStick"), DeviceAction::Axis, {1.0, 0.0}}});
    CHECK(tracker.update({.connected = true, .buttons = xinput::kA, .leftX = 32767, .leftY = 0}).empty());

    CHECK(tracker.update({.connected = true, .buttons = xinput::kA, .leftTrigger = 25, .leftX = 32767}).empty());
    CHECK(listOf(tracker.update({.connected = true, .buttons = xinput::kA, .leftTrigger = 31, .leftX = 32767})) ==
          std::vector<DeviceEvent>{down("Gamepad.LeftTrigger")});
    CHECK(tracker.update({.connected = true, .buttons = xinput::kA, .leftTrigger = 20, .leftX = 32767}).empty());
    CHECK(listOf(tracker.update({.connected = true, .buttons = xinput::kA, .leftTrigger = 19, .leftX = 32767})) ==
          std::vector<DeviceEvent>{up("Gamepad.LeftTrigger")});

    CHECK(listOf(tracker.update({.connected = true,
                                 .buttons = xinput::kA | xinput::kDpadLeft,
                                 .rightTrigger = 255,
                                 .leftX = 32767,
                                 .rightX = -32768,
                                 .rightY = -32768})) ==
          std::vector<DeviceEvent>{
              down("Gamepad.DpadLeft"), down("Gamepad.RightTrigger"),
              {source("Gamepad.RightStick"), DeviceAction::Axis, {-1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0)}}});

    CHECK(listOf(tracker.update({.connected = false, .buttons = xinput::kB})) ==
          std::vector<DeviceEvent>{up("Gamepad.A"), up("Gamepad.DpadLeft"), up("Gamepad.RightTrigger"),
                                   {source("Gamepad.LeftStick"), DeviceAction::Axis, {0.0, 0.0}},
                                   {source("Gamepad.RightStick"), DeviceAction::Axis, {0.0, 0.0}}});
    CHECK(tracker.update({}).empty());
}

TEST_CASE("sticks ignore a small drift around the center and still reach full tilt (INP-023)")
{
    GamepadTracker tracker;
    CHECK(tracker.update({.connected = true, .leftX = 2000, .leftY = -1500}).empty());
    CHECK(tracker.update({.connected = true, .leftX = -1500, .leftY = 1800}).empty());

    const auto half = listOf(tracker.update({.connected = true, .leftX = 16384}));
    REQUIRE(half.size() == 1);
    CHECK(half[0].value.x == doctest::Approx((16384.0 / 32767.0 - 0.08) / 0.92));
    CHECK(half[0].value.y == 0.0);

    CHECK(listOf(tracker.update({.connected = true, .leftX = 32767})) ==
          std::vector<DeviceEvent>{{source("Gamepad.LeftStick"), DeviceAction::Axis, {1.0, 0.0}}});
    CHECK(listOf(tracker.update({.connected = true, .leftX = 1000})) ==
          std::vector<DeviceEvent>{{source("Gamepad.LeftStick"), DeviceAction::Axis, {0.0, 0.0}}});
    CHECK(tracker.update({.connected = true, .leftX = 1200, .rightY = -900}).empty());
}

TEST_CASE("each XInput button bit names its own gamepad source (ADR-0069)")
{
    const std::vector<std::pair<std::uint16_t, std::string_view>> buttons{
        {xinput::kA, "Gamepad.A"},
        {xinput::kB, "Gamepad.B"},
        {xinput::kX, "Gamepad.X"},
        {xinput::kY, "Gamepad.Y"},
        {xinput::kLeftShoulder, "Gamepad.LeftShoulder"},
        {xinput::kRightShoulder, "Gamepad.RightShoulder"},
        {xinput::kLeftThumb, "Gamepad.LeftThumb"},
        {xinput::kRightThumb, "Gamepad.RightThumb"},
        {xinput::kStart, "Gamepad.Start"},
        {xinput::kBack, "Gamepad.Back"},
        {xinput::kDpadUp, "Gamepad.DpadUp"},
        {xinput::kDpadDown, "Gamepad.DpadDown"},
        {xinput::kDpadLeft, "Gamepad.DpadLeft"},
        {xinput::kDpadRight, "Gamepad.DpadRight"},
    };
    for (const auto& [bit, name] : buttons) {
        GamepadTracker tracker;
        CHECK(listOf(tracker.update({.connected = true, .buttons = bit})) == std::vector<DeviceEvent>{down(name)});
    }
}

TEST_CASE("decoded events drive the mapping engine (ADR-0069)")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"jump","type":"hold","inputs":{"press":"KeyW"}},
        {"id":"move","type":"padStick","anchor":"left","offset":[0.3,0],"inputs":{"stick":"Gamepad.LeftStick"}}]})");
    REQUIRE(loaded.ok());
    Touches touches;
    Listener listener;
    MappingEngine engine(loaded.profile, {.screen = {.width = 2400, .height = 1080}}, touches, listener);

    KeyboardDecoder keyboard;
    deliver(*keyboard.decode(0x11, false, false, false), engine, Microseconds{0});
    CHECK(engine.activeTouches() == 1);

    GamepadTracker pad;
    for (const DeviceEvent& event : pad.update({.connected = true, .leftX = 32767})) {
        deliver(event, engine, Microseconds{1000});
    }
    CHECK(engine.activeTouches() == 2);

    deliver(*keyboard.decode(0x11, false, false, true), engine, Microseconds{2000});
    for (const DeviceEvent& event : pad.update({})) {
        deliver(event, engine, Microseconds{3000});
    }
    engine.flush(Microseconds{100'000});
    CHECK(engine.activeTouches() == 0);
    CHECK_FALSE(touches.frames.empty());
}
