#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::input {

enum class Device : std::uint8_t { Keyboard, Mouse, Gamepad, Extension };

enum class SourceKind : std::uint8_t { Button, Axis1D, Axis2D };

struct InputSource {
    Device device = Device::Keyboard;
    std::uint16_t code = 0;

    friend auto operator<=>(const InputSource&, const InputSource&) = default;
};

namespace modifier {
inline constexpr std::uint8_t kCtrl = 0x01;
inline constexpr std::uint8_t kShift = 0x02;
inline constexpr std::uint8_t kAlt = 0x04;
}

struct Chord {
    std::uint8_t modifiers = 0;
    InputSource source;

    friend auto operator<=>(const Chord&, const Chord&) = default;
};

inline constexpr std::size_t kKeyboardSourceCount = 117;
inline constexpr std::size_t kMouseSourceCount = 10;
inline constexpr std::size_t kGamepadSourceCount = 18;
inline constexpr std::size_t kExtensionSourceCount = 256;
inline constexpr std::size_t kSourceCount =
    kKeyboardSourceCount + kMouseSourceCount + kGamepadSourceCount + kExtensionSourceCount;

inline constexpr std::string_view kExtensionInputPrefix = "Ext.";
inline constexpr std::string_view kOwnInputs = "mine";
inline constexpr std::size_t kMostInputPartLength = 32;

struct ExtensionInput {
    std::string extension;
    std::string source;
    std::string key;

    friend bool operator==(const ExtensionInput&, const ExtensionInput&) = default;
};

[[nodiscard]] bool validInputSourceName(std::string_view source) noexcept;
[[nodiscard]] bool validInputKeyName(std::string_view key) noexcept;
[[nodiscard]] std::optional<ExtensionInput> splitExtensionInput(std::string_view name);
[[nodiscard]] std::string extensionInputName(std::string_view extension, std::string_view source,
                                             std::string_view key);
[[nodiscard]] std::optional<InputSource> knownSource(std::string_view name);

[[nodiscard]] std::optional<InputSource> parseSource(std::string_view name);
[[nodiscard]] std::string sourceName(InputSource source);
[[nodiscard]] SourceKind kindOf(InputSource source) noexcept;
[[nodiscard]] bool isModifierKey(InputSource source) noexcept;

[[nodiscard]] std::size_t denseIndex(InputSource source) noexcept;

[[nodiscard]] std::uint8_t modifierBit(InputSource source) noexcept;

[[nodiscard]] std::optional<Chord> parseChord(std::string_view text);
[[nodiscard]] std::string chordName(const Chord& chord);
[[nodiscard]] std::string chordName(const std::optional<Chord>& chord);

}
