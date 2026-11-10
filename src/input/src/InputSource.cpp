#include "gs/input/InputSource.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <map>
#include <mutex>
#include <vector>

namespace gs::input {

namespace {

constexpr std::uint16_t kLetterBase = 0;
constexpr std::uint16_t kDigitBase = 26;
constexpr std::uint16_t kFunctionBase = 36;
constexpr std::uint16_t kFunctionCount = 24;
constexpr std::uint16_t kNumpadBase = 60;
constexpr std::uint16_t kNamedBase = 70;

constexpr std::array<std::string_view, 47> kNamedKeys{
    "Space", "Enter", "Escape", "Tab", "Backspace",
    "ShiftLeft", "ShiftRight", "ControlLeft", "ControlRight", "AltLeft", "AltRight",
    "MetaLeft", "MetaRight", "CapsLock",
    "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
    "Insert", "Delete", "Home", "End", "PageUp", "PageDown",
    "NumpadAdd", "NumpadSubtract", "NumpadMultiply", "NumpadDivide", "NumpadDecimal", "NumpadEnter",
    "NumLock",
    "Backquote", "Minus", "Equal", "BracketLeft", "BracketRight", "Backslash", "Semicolon", "Quote",
    "Comma", "Period", "Slash", "IntlBackslash",
    "PrintScreen", "ScrollLock", "Pause", "ContextMenu",
};

constexpr std::array<std::string_view, 10> kMouseNames{
    "Left", "Right", "Middle", "X1", "X2", "WheelUp", "WheelDown", "WheelLeft", "WheelRight", "Move",
};
constexpr std::uint16_t kMouseMove = 9;

constexpr std::array<std::string_view, 18> kGamepadNames{
    "A", "B", "X", "Y", "LeftShoulder", "RightShoulder", "LeftThumb", "RightThumb", "Start", "Back",
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight",
    "LeftTrigger", "RightTrigger", "LeftStick", "RightStick",
};
constexpr std::uint16_t kGamepadFirstTrigger = 14;
constexpr std::uint16_t kGamepadFirstStick = 16;

static_assert(kNamedBase + kNamedKeys.size() == kKeyboardSourceCount);
static_assert(kMouseNames.size() == kMouseSourceCount);
static_assert(kGamepadNames.size() == kGamepadSourceCount);

constexpr std::string_view kMousePrefix = "Mouse.";
constexpr std::string_view kGamepadPrefix = "Gamepad.";

template <std::size_t N>
std::optional<std::uint16_t> indexOf(const std::array<std::string_view, N>& names, std::string_view name)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == name) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return std::nullopt;
}

std::optional<unsigned> parseNumber(std::string_view text)
{
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint16_t> parseKeyCode(std::string_view name)
{
    if (name.size() == 4 && name.starts_with("Key") && name[3] >= 'A' && name[3] <= 'Z') {
        return static_cast<std::uint16_t>(kLetterBase + (name[3] - 'A'));
    }
    if (name.size() == 6 && name.starts_with("Digit") && name[5] >= '0' && name[5] <= '9') {
        return static_cast<std::uint16_t>(kDigitBase + (name[5] - '0'));
    }
    if (name.size() == 7 && name.starts_with("Numpad") && name[6] >= '0' && name[6] <= '9') {
        return static_cast<std::uint16_t>(kNumpadBase + (name[6] - '0'));
    }
    if (name.starts_with('F')) {
        if (const auto number = parseNumber(name.substr(1)); number && *number >= 1 && *number <= kFunctionCount) {
            return static_cast<std::uint16_t>(kFunctionBase + *number - 1);
        }
    }
    if (const auto index = indexOf(kNamedKeys, name)) {
        return static_cast<std::uint16_t>(kNamedBase + *index);
    }
    return std::nullopt;
}

std::string keyName(std::uint16_t code)
{
    if (code < kDigitBase) {
        return std::string("Key") + static_cast<char>('A' + (code - kLetterBase));
    }
    if (code < kFunctionBase) {
        return std::string("Digit") + static_cast<char>('0' + (code - kDigitBase));
    }
    if (code < kNumpadBase) {
        return "F" + std::to_string(code - kFunctionBase + 1);
    }
    if (code < kNamedBase) {
        return std::string("Numpad") + static_cast<char>('0' + (code - kNumpadBase));
    }
    const std::size_t index = code - kNamedBase;
    return index < kNamedKeys.size() ? std::string(kNamedKeys[index]) : std::string();
}

std::uint16_t namedKey(std::string_view name)
{
    return static_cast<std::uint16_t>(kNamedBase + *indexOf(kNamedKeys, name));
}

constexpr std::size_t kMostExtensionIdLength = 64;

bool lowerOrDigit(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool extensionIdPart(std::string_view part) noexcept
{
    return !part.empty() && part.front() != '-' && part.back() != '-' && part.find("--") == std::string_view::npos &&
           std::ranges::all_of(part, [](char c) { return lowerOrDigit(c) || c == '-'; });
}

bool validExtensionOwner(std::string_view owner) noexcept
{
    if (owner == kOwnInputs) {
        return true;
    }
    if (owner.empty() || owner.size() > kMostExtensionIdLength) {
        return false;
    }
    std::size_t parts = 0;
    while (true) {
        const std::size_t dot = owner.find('.');
        if (!extensionIdPart(owner.substr(0, dot))) {
            return false;
        }
        ++parts;
        if (dot == std::string_view::npos) {
            break;
        }
        owner.remove_prefix(dot + 1);
    }
    return parts >= 2;
}

struct ExtensionNames {
    std::mutex mutex;
    std::vector<std::string> names;
    std::map<std::string, std::uint16_t, std::less<>> codes;
};

ExtensionNames& extensionNames()
{
    static ExtensionNames table;
    return table;
}

std::optional<InputSource> extensionSource(std::string_view name, bool remember)
{
    if (!splitExtensionInput(name)) {
        return std::nullopt;
    }
    ExtensionNames& table = extensionNames();
    const std::scoped_lock lock(table.mutex);
    if (const auto found = table.codes.find(name); found != table.codes.end()) {
        return InputSource{Device::Extension, found->second};
    }
    if (!remember || table.names.size() >= kExtensionSourceCount) {
        return std::nullopt;
    }
    const auto code = static_cast<std::uint16_t>(table.names.size());
    table.names.emplace_back(name);
    table.codes.emplace(std::string(name), code);
    return InputSource{Device::Extension, code};
}

}

bool validInputSourceName(std::string_view source) noexcept
{
    return !source.empty() && source.size() <= kMostInputPartLength &&
           std::ranges::all_of(source, [](char c) { return lowerOrDigit(c) || c == '-'; });
}

bool validInputKeyName(std::string_view key) noexcept
{
    return !key.empty() && key.size() <= kMostInputPartLength && std::ranges::all_of(key, [](char c) {
        return lowerOrDigit(c) || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
    });
}

std::optional<ExtensionInput> splitExtensionInput(std::string_view name)
{
    if (!name.starts_with(kExtensionInputPrefix)) {
        return std::nullopt;
    }
    name.remove_prefix(kExtensionInputPrefix.size());
    const std::size_t keyDot = name.rfind('.');
    if (keyDot == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t sourceDot = name.rfind('.', keyDot == 0 ? 0 : keyDot - 1);
    if (sourceDot == std::string_view::npos || sourceDot >= keyDot) {
        return std::nullopt;
    }
    const std::string_view owner = name.substr(0, sourceDot);
    const std::string_view source = name.substr(sourceDot + 1, keyDot - sourceDot - 1);
    const std::string_view key = name.substr(keyDot + 1);
    if (!validExtensionOwner(owner) || !validInputSourceName(source) || !validInputKeyName(key)) {
        return std::nullopt;
    }
    return ExtensionInput{std::string(owner), std::string(source), std::string(key)};
}

std::string extensionInputName(std::string_view extension, std::string_view source, std::string_view key)
{
    return std::string(kExtensionInputPrefix) + std::string(extension) + "." + std::string(source) + "." +
           std::string(key);
}

std::optional<InputSource> knownSource(std::string_view name)
{
    if (name.starts_with(kExtensionInputPrefix)) {
        return extensionSource(name, false);
    }
    return parseSource(name);
}

std::optional<InputSource> parseSource(std::string_view name)
{
    if (name.starts_with(kExtensionInputPrefix)) {
        return extensionSource(name, true);
    }
    if (name.starts_with(kMousePrefix)) {
        if (const auto code = indexOf(kMouseNames, name.substr(kMousePrefix.size()))) {
            return InputSource{Device::Mouse, *code};
        }
        return std::nullopt;
    }
    if (name.starts_with(kGamepadPrefix)) {
        if (const auto code = indexOf(kGamepadNames, name.substr(kGamepadPrefix.size()))) {
            return InputSource{Device::Gamepad, *code};
        }
        return std::nullopt;
    }
    if (const auto code = parseKeyCode(name)) {
        return InputSource{Device::Keyboard, *code};
    }
    return std::nullopt;
}

std::string sourceName(InputSource source)
{
    switch (source.device) {
    case Device::Keyboard:
        return keyName(source.code);
    case Device::Mouse:
        return source.code < kMouseNames.size() ? std::string(kMousePrefix) + std::string(kMouseNames[source.code])
                                                : std::string();
    case Device::Gamepad:
        return source.code < kGamepadNames.size()
                   ? std::string(kGamepadPrefix) + std::string(kGamepadNames[source.code])
                   : std::string();
    case Device::Extension: {
        ExtensionNames& table = extensionNames();
        const std::scoped_lock lock(table.mutex);
        return source.code < table.names.size() ? table.names[source.code] : std::string();
    }
    }
    return {};
}

SourceKind kindOf(InputSource source) noexcept
{
    if (source.device == Device::Mouse && source.code == kMouseMove) {
        return SourceKind::Axis2D;
    }
    if (source.device == Device::Gamepad) {
        if (source.code >= kGamepadFirstStick) {
            return SourceKind::Axis2D;
        }
        if (source.code >= kGamepadFirstTrigger) {
            return SourceKind::Axis1D;
        }
    }
    return SourceKind::Button;
}

bool isModifierKey(InputSource source) noexcept
{
    return modifierBit(source) != 0;
}

std::uint8_t modifierBit(InputSource source) noexcept
{
    if (source.device != Device::Keyboard) {
        return 0;
    }
    static const std::uint16_t kShiftLeft = namedKey("ShiftLeft");
    static const std::uint16_t kShiftRight = namedKey("ShiftRight");
    static const std::uint16_t kControlLeft = namedKey("ControlLeft");
    static const std::uint16_t kControlRight = namedKey("ControlRight");
    static const std::uint16_t kAltLeft = namedKey("AltLeft");
    static const std::uint16_t kAltRight = namedKey("AltRight");
    if (source.code == kControlLeft || source.code == kControlRight) {
        return modifier::kCtrl;
    }
    if (source.code == kShiftLeft || source.code == kShiftRight) {
        return modifier::kShift;
    }
    if (source.code == kAltLeft || source.code == kAltRight) {
        return modifier::kAlt;
    }
    return 0;
}

std::size_t denseIndex(InputSource source) noexcept
{
    switch (source.device) {
    case Device::Keyboard:
        return std::min<std::size_t>(source.code, kKeyboardSourceCount - 1);
    case Device::Mouse:
        return kKeyboardSourceCount + std::min<std::size_t>(source.code, kMouseSourceCount - 1);
    case Device::Gamepad:
        return kKeyboardSourceCount + kMouseSourceCount + std::min<std::size_t>(source.code, kGamepadSourceCount - 1);
    case Device::Extension:
        return kKeyboardSourceCount + kMouseSourceCount + kGamepadSourceCount +
               std::min<std::size_t>(source.code, kExtensionSourceCount - 1);
    }
    return 0;
}

std::optional<Chord> parseChord(std::string_view text)
{
    Chord chord;
    while (true) {
        const std::size_t plus = text.find('+');
        if (plus == std::string_view::npos) {
            break;
        }
        const std::string_view part = text.substr(0, plus);
        std::uint8_t bit = 0;
        if (part == "Ctrl") {
            bit = modifier::kCtrl;
        } else if (part == "Shift") {
            bit = modifier::kShift;
        } else if (part == "Alt") {
            bit = modifier::kAlt;
        }
        if (bit == 0 || (chord.modifiers & bit) != 0) {
            return std::nullopt;
        }
        chord.modifiers = static_cast<std::uint8_t>(chord.modifiers | bit);
        text.remove_prefix(plus + 1);
    }
    const auto source = parseSource(text);
    if (!source) {
        return std::nullopt;
    }
    if (chord.modifiers != 0 && (kindOf(*source) != SourceKind::Button || isModifierKey(*source))) {
        return std::nullopt;
    }
    chord.source = *source;
    return chord;
}

std::string chordName(const Chord& chord)
{
    std::string text;
    if ((chord.modifiers & modifier::kCtrl) != 0) {
        text += "Ctrl+";
    }
    if ((chord.modifiers & modifier::kShift) != 0) {
        text += "Shift+";
    }
    if ((chord.modifiers & modifier::kAlt) != 0) {
        text += "Alt+";
    }
    return text + sourceName(chord.source);
}

std::string chordName(const std::optional<Chord>& chord)
{
    return chord ? chordName(*chord) : std::string();
}

}
