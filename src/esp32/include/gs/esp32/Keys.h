#pragma once

#include <gs_esp32_protocol.h>

#include <array>
#include <cstdint>
#include <optional>

namespace gs::esp32 {

enum class UsageKind : std::uint8_t { Keyboard, Modifier, Consumer };

struct HidUsage {
    UsageKind kind = UsageKind::Keyboard;
    std::uint16_t usage = 0;

    friend bool operator==(const HidUsage&, const HidUsage&) = default;
};

struct TypedKey {
    std::uint8_t usage = 0;
    bool shift = false;

    friend bool operator==(const TypedKey&, const TypedKey&) = default;
};

inline constexpr std::uint8_t kLeftShiftBit = 0x02;

[[nodiscard]] std::optional<HidUsage> hidUsageOf(std::uint32_t androidKeycode) noexcept;
[[nodiscard]] std::optional<TypedKey> typedKeyOf(char character) noexcept;

class KeyboardState
{
public:
    [[nodiscard]] bool apply(const HidUsage& usage, bool down) noexcept;
    [[nodiscard]] gs_esp32_keyboard report() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;

private:
    std::uint8_t m_modifiers = 0;
    std::array<std::uint8_t, GS_ESP32_KEYS> m_keys{};
};

}
