#include "gs/esp32/Keys.h"

#include <algorithm>
#include <string_view>

namespace gs::esp32 {

namespace {

struct KeyEntry {
    std::uint16_t keycode = 0;
    HidUsage usage;
};

constexpr HidUsage key(std::uint16_t usage) noexcept
{
    return {.kind = UsageKind::Keyboard, .usage = usage};
}

constexpr HidUsage modifier(std::uint16_t bit) noexcept
{
    return {.kind = UsageKind::Modifier, .usage = bit};
}

constexpr HidUsage consumer(std::uint16_t usage) noexcept
{
    return {.kind = UsageKind::Consumer, .usage = usage};
}

constexpr std::array kKeys{
    KeyEntry{3, consumer(0x0223)},   KeyEntry{4, consumer(0x0224)},   KeyEntry{17, key(0x55)},
    KeyEntry{19, key(0x52)},         KeyEntry{20, key(0x51)},         KeyEntry{21, key(0x50)},
    KeyEntry{22, key(0x4F)},         KeyEntry{23, key(0x28)},         KeyEntry{24, consumer(0x00E9)},
    KeyEntry{25, consumer(0x00EA)},  KeyEntry{26, consumer(0x0030)},  KeyEntry{55, key(0x36)},
    KeyEntry{56, key(0x37)},         KeyEntry{57, modifier(0x04)},    KeyEntry{58, modifier(0x40)},
    KeyEntry{59, modifier(0x02)},    KeyEntry{60, modifier(0x20)},    KeyEntry{61, key(0x2B)},
    KeyEntry{62, key(0x2C)},         KeyEntry{64, consumer(0x0196)},  KeyEntry{65, consumer(0x018A)},
    KeyEntry{66, key(0x28)},         KeyEntry{67, key(0x2A)},         KeyEntry{68, key(0x35)},
    KeyEntry{69, key(0x2D)},         KeyEntry{70, key(0x2E)},         KeyEntry{71, key(0x2F)},
    KeyEntry{72, key(0x30)},         KeyEntry{73, key(0x31)},         KeyEntry{74, key(0x33)},
    KeyEntry{75, key(0x34)},         KeyEntry{76, key(0x38)},         KeyEntry{82, consumer(0x0040)},
    KeyEntry{84, consumer(0x0221)},  KeyEntry{85, consumer(0x00CD)},  KeyEntry{86, consumer(0x00B7)},
    KeyEntry{87, consumer(0x00B5)},  KeyEntry{88, consumer(0x00B6)},  KeyEntry{89, consumer(0x00B4)},
    KeyEntry{90, consumer(0x00B3)},  KeyEntry{92, key(0x4B)},         KeyEntry{93, key(0x4E)},
    KeyEntry{111, key(0x29)},        KeyEntry{112, key(0x4C)},        KeyEntry{113, modifier(0x01)},
    KeyEntry{114, modifier(0x10)},   KeyEntry{115, key(0x39)},        KeyEntry{116, key(0x47)},
    KeyEntry{117, modifier(0x08)},   KeyEntry{118, modifier(0x80)},   KeyEntry{120, key(0x46)},
    KeyEntry{121, key(0x48)},        KeyEntry{122, key(0x4A)},        KeyEntry{123, key(0x4D)},
    KeyEntry{124, key(0x49)},        KeyEntry{125, consumer(0x0225)}, KeyEntry{126, consumer(0x00B0)},
    KeyEntry{127, consumer(0x00B1)}, KeyEntry{143, key(0x53)},        KeyEntry{154, key(0x54)},
    KeyEntry{155, key(0x55)},        KeyEntry{156, key(0x56)},        KeyEntry{157, key(0x57)},
    KeyEntry{158, key(0x63)},        KeyEntry{159, key(0x85)},        KeyEntry{160, key(0x58)},
    KeyEntry{161, key(0x67)},        KeyEntry{164, consumer(0x00E2)}, KeyEntry{187, consumer(0x01A2)},
    KeyEntry{220, consumer(0x0070)}, KeyEntry{221, consumer(0x006F)},
};

static_assert(std::ranges::is_sorted(kKeys, {}, &KeyEntry::keycode));

constexpr std::uint32_t kKeycode0 = 7;
constexpr std::uint32_t kKeycode1 = 8;
constexpr std::uint32_t kKeycode9 = 16;
constexpr std::uint32_t kKeycodeA = 29;
constexpr std::uint32_t kKeycodeZ = 54;
constexpr std::uint32_t kKeycodeF1 = 131;
constexpr std::uint32_t kKeycodeF12 = 142;
constexpr std::uint32_t kKeycodeNumpad0 = 144;
constexpr std::uint32_t kKeycodeNumpad1 = 145;
constexpr std::uint32_t kKeycodeNumpad9 = 153;

constexpr std::string_view kShiftedDigits = ")!@#$%^&*(";

struct Punctuation {
    char plain = 0;
    char shifted = 0;
    std::uint8_t usage = 0;
};

constexpr std::array kPunctuation{
    Punctuation{'-', '_', 0x2D}, Punctuation{'=', '+', 0x2E}, Punctuation{'[', '{', 0x2F},
    Punctuation{']', '}', 0x30}, Punctuation{'\\', '|', 0x31}, Punctuation{';', ':', 0x33},
    Punctuation{'\'', '"', 0x34}, Punctuation{'`', '~', 0x35}, Punctuation{',', '<', 0x36},
    Punctuation{'.', '>', 0x37}, Punctuation{'/', '?', 0x38},
};

}

std::optional<HidUsage> hidUsageOf(std::uint32_t androidKeycode) noexcept
{
    if (androidKeycode >= kKeycodeA && androidKeycode <= kKeycodeZ) {
        return key(static_cast<std::uint16_t>(0x04 + androidKeycode - kKeycodeA));
    }
    if (androidKeycode == kKeycode0) {
        return key(0x27);
    }
    if (androidKeycode >= kKeycode1 && androidKeycode <= kKeycode9) {
        return key(static_cast<std::uint16_t>(0x1E + androidKeycode - kKeycode1));
    }
    if (androidKeycode >= kKeycodeF1 && androidKeycode <= kKeycodeF12) {
        return key(static_cast<std::uint16_t>(0x3A + androidKeycode - kKeycodeF1));
    }
    if (androidKeycode == kKeycodeNumpad0) {
        return key(0x62);
    }
    if (androidKeycode >= kKeycodeNumpad1 && androidKeycode <= kKeycodeNumpad9) {
        return key(static_cast<std::uint16_t>(0x59 + androidKeycode - kKeycodeNumpad1));
    }
    const auto found = std::ranges::lower_bound(kKeys, androidKeycode, {}, &KeyEntry::keycode);
    if (found == kKeys.end() || static_cast<std::uint32_t>(found->keycode) != androidKeycode) {
        return std::nullopt;
    }
    return found->usage;
}

std::optional<TypedKey> typedKeyOf(char character) noexcept
{
    if (character >= 'a' && character <= 'z') {
        return TypedKey{.usage = static_cast<std::uint8_t>(0x04 + (character - 'a')), .shift = false};
    }
    if (character >= 'A' && character <= 'Z') {
        return TypedKey{.usage = static_cast<std::uint8_t>(0x04 + (character - 'A')), .shift = true};
    }
    if (character == '0') {
        return TypedKey{.usage = 0x27, .shift = false};
    }
    if (character >= '1' && character <= '9') {
        return TypedKey{.usage = static_cast<std::uint8_t>(0x1E + (character - '1')), .shift = false};
    }
    if (const std::size_t digit = kShiftedDigits.find(character); digit != std::string_view::npos) {
        return TypedKey{.usage = static_cast<std::uint8_t>(digit == 0 ? 0x27 : 0x1E + digit - 1), .shift = true};
    }
    switch (character) {
    case ' ':
        return TypedKey{.usage = 0x2C, .shift = false};
    case '\n':
        return TypedKey{.usage = 0x28, .shift = false};
    case '\t':
        return TypedKey{.usage = 0x2B, .shift = false};
    default:
        break;
    }
    for (const Punctuation& entry : kPunctuation) {
        if (entry.plain == character) {
            return TypedKey{.usage = entry.usage, .shift = false};
        }
        if (entry.shifted == character) {
            return TypedKey{.usage = entry.usage, .shift = true};
        }
    }
    return std::nullopt;
}

bool KeyboardState::apply(const HidUsage& usage, bool down) noexcept
{
    if (usage.kind == UsageKind::Consumer) {
        return false;
    }
    if (usage.kind == UsageKind::Modifier) {
        const auto bit = static_cast<std::uint8_t>(usage.usage & 0xFFU);
        const auto next = static_cast<std::uint8_t>(down ? (m_modifiers | bit) : (m_modifiers & ~bit));
        const bool changed = next != m_modifiers;
        m_modifiers = next;
        return changed;
    }
    const auto code = static_cast<std::uint8_t>(usage.usage & 0xFFU);
    if (code == 0) {
        return false;
    }
    const auto held = std::ranges::find(m_keys, code);
    if (down) {
        if (held != m_keys.end()) {
            return false;
        }
        const auto vacant = std::ranges::find(m_keys, std::uint8_t{0});
        if (vacant == m_keys.end()) {
            return false;
        }
        *vacant = code;
        return true;
    }
    if (held == m_keys.end()) {
        return false;
    }
    *held = 0;
    std::size_t kept = 0;
    for (const std::uint8_t value : m_keys) {
        if (value != 0) {
            m_keys[kept++] = value;
        }
    }
    while (kept < m_keys.size()) {
        m_keys[kept++] = 0;
    }
    return true;
}

gs_esp32_keyboard KeyboardState::report() const noexcept
{
    gs_esp32_keyboard keyboard{};
    keyboard.modifiers = m_modifiers;
    std::ranges::copy(m_keys, keyboard.keys);
    return keyboard;
}

bool KeyboardState::empty() const noexcept
{
    return m_modifiers == 0 && std::ranges::all_of(m_keys, [](std::uint8_t value) { return value == 0; });
}

void KeyboardState::clear() noexcept
{
    m_modifiers = 0;
    m_keys = {};
}

}
