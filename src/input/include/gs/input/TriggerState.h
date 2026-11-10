#pragma once

#include <gs/base/Clock.h>

#include <cstdint>
#include <optional>

namespace gs::input {

enum class Trigger : std::uint8_t { Hold, Tap, LongPress, DoubleTap, Toggle, Turbo, Release };

struct TriggerConfig {
    Trigger trigger = Trigger::Hold;
    std::uint16_t longPressMs = 300;
    std::uint16_t doubleTapMs = 250;
    std::uint8_t rateHz = 10;
};

enum class TriggerSignal : std::uint8_t {
    None,
    Activate,
    Deactivate,
    Pulse,
};

class TriggerState
{
public:
    explicit TriggerState(const TriggerConfig& config = {},
                          std::optional<Microseconds> tapDecidedOnRelease = std::nullopt) noexcept;

    [[nodiscard]] TriggerSignal press(Microseconds now) noexcept;
    [[nodiscard]] TriggerSignal release(Microseconds now) noexcept;
    [[nodiscard]] TriggerSignal tick(Microseconds now) noexcept;

    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return m_deadline; }
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] Trigger kind() const noexcept { return m_trigger; }

    void reset() noexcept;
    void retune(const TriggerConfig& config) noexcept;

private:
    Trigger m_trigger;
    Microseconds m_longPress;
    Microseconds m_doubleTapWindow;
    Microseconds m_turboPeriod;
    std::optional<Microseconds> m_tapDecidedOnRelease;

    bool m_pressed = false;
    bool m_active = false;
    bool m_firstTapSeen = false;
    Microseconds m_pressedAt{0};
    std::optional<Microseconds> m_deadline;
};

}
