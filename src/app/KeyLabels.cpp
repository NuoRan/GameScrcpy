#include "KeyLabels.h"

#include <gs/input/DeviceEvents.h>
#include <gs/platform/KeyboardLayout.h>

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace gs::app {

namespace {

using Names = std::pair<std::string_view, std::string_view>;

constexpr std::string_view kMousePrefix = "Mouse.";
constexpr std::string_view kGamepadPrefix = "Gamepad.";
constexpr std::string_view kNumpadPrefix = "Numpad";

constexpr auto kKeyNames = std::to_array<Names>({
    {"Escape", "Esc"},          {"Backspace", "Bksp"},     {"CapsLock", "Caps"},      {"ShiftLeft", "Shift"},
    {"ShiftRight", "Shift"},    {"ControlLeft", "Ctrl"},   {"ControlRight", "Ctrl"},  {"AltLeft", "Alt"},
    {"AltRight", "Alt"},        {"MetaLeft", "Win"},       {"MetaRight", "Win"},      {"ContextMenu", "Menu"},
    {"ArrowUp", "\u2191"},      {"ArrowDown", "\u2193"},   {"ArrowLeft", "\u2190"},   {"ArrowRight", "\u2192"},
    {"Insert", "Ins"},          {"Delete", "Del"},         {"PageUp", "PgUp"},        {"PageDown", "PgDn"},
    {"PrintScreen", "PrtSc"},   {"ScrollLock", "ScrLk"},   {"NumLock", "NumLk"},      {"NumpadAdd", "Num+"},
    {"NumpadSubtract", "Num-"}, {"NumpadMultiply", "Num*"}, {"NumpadDivide", "Num/"}, {"NumpadDecimal", "Num."},
    {"NumpadEnter", "Num Enter"},
});

constexpr auto kGamepadNames = std::to_array<Names>({
    {"LeftShoulder", "LB"}, {"RightShoulder", "RB"}, {"LeftThumb", "L3"},   {"RightThumb", "R3"},
    {"LeftTrigger", "LT"},  {"RightTrigger", "RT"},  {"LeftStick", "LS"},   {"RightStick", "RS"},
    {"DpadUp", "D\u2191"},  {"DpadDown", "D\u2193"}, {"DpadLeft", "D\u2190"}, {"DpadRight", "D\u2192"},
});

QString lookup(std::string_view name, const auto& table)
{
    for (const auto& [key, text] : table) {
        if (key == name) {
            return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
        }
    }
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

QString keyboardText(input::InputSource source, std::string_view name)
{
    if (name == "Space") {
        return QCoreApplication::translate("KeyLabels", "Space");
    }
    if (name.starts_with(kNumpadPrefix) && name.size() == kNumpadPrefix.size() + 1) {
        return QStringLiteral("Num") + QChar::fromLatin1(name.back());
    }
    const bool named = std::ranges::any_of(kKeyNames, [&](const Names& entry) { return entry.first == name; });
    if (!named) {
        if (const std::optional<std::uint16_t> code = input::scanCodeOf(source)) {
            if (const std::optional<char32_t> character = platform::layoutCharacter(*code)) {
                return QString::fromUcs4(&*character, 1).toUpper();
            }
        }
    }
    return lookup(name, kKeyNames);
}

QString mouseText(std::string_view name)
{
    if (name == "Left") {
        return QCoreApplication::translate("KeyLabels", "Left button");
    }
    if (name == "Right") {
        return QCoreApplication::translate("KeyLabels", "Right button");
    }
    if (name == "Middle") {
        return QCoreApplication::translate("KeyLabels", "Middle button");
    }
    if (name == "X1") {
        return QCoreApplication::translate("KeyLabels", "Back button");
    }
    if (name == "X2") {
        return QCoreApplication::translate("KeyLabels", "Forward button");
    }
    if (name == "WheelUp") {
        return QCoreApplication::translate("KeyLabels", "Wheel up");
    }
    if (name == "WheelDown") {
        return QCoreApplication::translate("KeyLabels", "Wheel down");
    }
    if (name == "WheelLeft") {
        return QCoreApplication::translate("KeyLabels", "Wheel left");
    }
    if (name == "WheelRight") {
        return QCoreApplication::translate("KeyLabels", "Wheel right");
    }
    if (name == "Move") {
        return QCoreApplication::translate("KeyLabels", "Mouse movement");
    }
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

QString mouseGlyph(std::string_view name)
{
    constexpr auto kGlyphs = std::to_array<Names>({
        {"Left", "left"},
        {"Right", "right"},
        {"Middle", "middle"},
        {"X1", "back"},
        {"X2", "forward"},
        {"WheelUp", "wheelUp"},
        {"WheelDown", "wheelDown"},
        {"WheelLeft", "wheelLeft"},
        {"WheelRight", "wheelRight"},
    });
    for (const auto& [key, glyph] : kGlyphs) {
        if (key == name) {
            return QString::fromUtf8(glyph.data(), static_cast<qsizetype>(glyph.size()));
        }
    }
    return {};
}

QString mouseHint(std::string_view name)
{
    if (name == "Left") {
        return QCoreApplication::translate("KeyLabels", "LMB");
    }
    if (name == "Right") {
        return QCoreApplication::translate("KeyLabels", "RMB");
    }
    if (name == "Middle") {
        return QCoreApplication::translate("KeyLabels", "MMB");
    }
    if (name == "X1") {
        return QCoreApplication::translate("KeyLabels", "M4");
    }
    if (name == "X2") {
        return QCoreApplication::translate("KeyLabels", "M5");
    }
    if (name == "WheelUp") {
        return QCoreApplication::translate("KeyLabels", "Wheel \u2191");
    }
    if (name == "WheelDown") {
        return QCoreApplication::translate("KeyLabels", "Wheel \u2193");
    }
    if (name == "WheelLeft") {
        return QCoreApplication::translate("KeyLabels", "Wheel \u2190");
    }
    if (name == "WheelRight") {
        return QCoreApplication::translate("KeyLabels", "Wheel \u2192");
    }
    return mouseText(name);
}

QString extensionText(std::string_view name)
{
    const std::optional<input::ExtensionInput> parts = input::splitExtensionInput(name);
    if (!parts) {
        return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    }
    return QCoreApplication::translate("KeyLabels", "%1 \u00B7 %2")
        .arg(QString::fromStdString(parts->source), QString::fromStdString(parts->key));
}

}

std::vector<KeyLabel> keyCaps(const input::Chord& chord)
{
    std::vector<KeyLabel> caps;
    if ((chord.modifiers & input::modifier::kCtrl) != 0) {
        caps.push_back({QStringLiteral("Ctrl"), false});
    }
    if ((chord.modifiers & input::modifier::kShift) != 0) {
        caps.push_back({QStringLiteral("Shift"), false});
    }
    if ((chord.modifiers & input::modifier::kAlt) != 0) {
        caps.push_back({QStringLiteral("Alt"), false});
    }
    const std::string name = input::sourceName(chord.source);
    switch (chord.source.device) {
    case input::Device::Mouse:
        caps.push_back({mouseText(std::string_view(name).substr(kMousePrefix.size())), true});
        break;
    case input::Device::Gamepad:
        caps.push_back({lookup(std::string_view(name).substr(kGamepadPrefix.size()), kGamepadNames), false});
        break;
    case input::Device::Keyboard:
        caps.push_back({keyboardText(chord.source, name), false});
        break;
    case input::Device::Extension:
        caps.push_back({extensionText(name), false});
        break;
    }
    return caps;
}

KeyLabel keyLabel(const input::Chord& chord)
{
    QString prefix;
    if ((chord.modifiers & input::modifier::kCtrl) != 0) {
        prefix += QStringLiteral("Ctrl+");
    }
    if ((chord.modifiers & input::modifier::kShift) != 0) {
        prefix += QStringLiteral("Shift+");
    }
    if ((chord.modifiers & input::modifier::kAlt) != 0) {
        prefix += QStringLiteral("Alt+");
    }
    const std::string name = input::sourceName(chord.source);
    switch (chord.source.device) {
    case input::Device::Mouse: {
        const std::string_view button = std::string_view(name).substr(kMousePrefix.size());
        return {prefix + mouseHint(button), true, mouseGlyph(button), prefix};
    }
    case input::Device::Gamepad:
        return {prefix + lookup(std::string_view(name).substr(kGamepadPrefix.size()), kGamepadNames), false};
    case input::Device::Extension:
        return {prefix + extensionText(name), false};
    case input::Device::Keyboard:
        break;
    }
    return {prefix + keyboardText(chord.source, name), false};
}

}
