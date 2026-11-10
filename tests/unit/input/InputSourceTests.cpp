#include "gs/input/InputSource.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace gs::input;

TEST_CASE("every input source name round-trips")
{
    std::vector<std::string> names{"KeyA", "KeyZ", "Digit0", "Digit9", "F1", "F12", "F24", "Numpad0",
                                   "Numpad9", "Space", "Escape", "ShiftLeft", "ControlRight", "AltLeft",
                                   "ArrowUp", "NumpadEnter", "Backquote", "ContextMenu"};
    for (const char* mouse : {"Left", "Right", "Middle", "X1", "X2", "WheelUp", "WheelDown", "WheelLeft",
                              "WheelRight", "Move"}) {
        names.push_back(std::string("Mouse.") + mouse);
    }
    for (const char* pad : {"A", "B", "X", "Y", "LeftShoulder", "RightShoulder", "LeftThumb", "RightThumb",
                            "Start", "Back", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "LeftTrigger",
                            "RightTrigger", "LeftStick", "RightStick"}) {
        names.push_back(std::string("Gamepad.") + pad);
    }
    for (const std::string& name : names) {
        INFO(name);
        const auto source = parseSource(name);
        REQUIRE(source.has_value());
        CHECK(sourceName(*source) == name);
    }
}

TEST_CASE("unknown or legacy-style names are rejected")
{
    for (const char* name : {"", "W", "Key_W", "MouseLeft", "WheelUp", "F0", "F25", "F01", "Digit10", "Keya",
                             "Mouse.", "Mouse.Up", "Gamepad.Z", "mouse.Left", "↑"}) {
        INFO(name);
        CHECK_FALSE(parseSource(name).has_value());
    }
}

TEST_CASE("source kinds distinguish buttons, triggers and two-dimensional inputs")
{
    CHECK(kindOf(*parseSource("KeyW")) == SourceKind::Button);
    CHECK(kindOf(*parseSource("Mouse.WheelUp")) == SourceKind::Button);
    CHECK(kindOf(*parseSource("Mouse.Move")) == SourceKind::Axis2D);
    CHECK(kindOf(*parseSource("Gamepad.LeftTrigger")) == SourceKind::Axis1D);
    CHECK(kindOf(*parseSource("Gamepad.RightStick")) == SourceKind::Axis2D);
    CHECK(kindOf(*parseSource("Gamepad.A")) == SourceKind::Button);
}

TEST_CASE("chords combine modifiers with one non-modifier button")
{
    const auto chord = parseChord("Ctrl+Shift+KeyG");
    REQUIRE(chord.has_value());
    CHECK(chord->modifiers == (modifier::kCtrl | modifier::kShift));
    CHECK(sourceName(chord->source) == "KeyG");
    CHECK(chordName(*chord) == "Ctrl+Shift+KeyG");

    CHECK(chordName(*parseChord("Shift+Alt+Ctrl+Mouse.Left")) == "Ctrl+Shift+Alt+Mouse.Left");
    CHECK(chordName(*parseChord("Space")) == "Space");

    for (const char* text : {"Ctrl+", "Ctrl+Ctrl+KeyA", "Win+KeyA", "Ctrl+ShiftLeft", "Alt+Mouse.Move",
                             "Ctrl+Gamepad.LeftStick", "Ctrl+Gamepad.LeftTrigger", "+KeyA"}) {
        INFO(text);
        CHECK_FALSE(parseChord(text).has_value());
    }
}
