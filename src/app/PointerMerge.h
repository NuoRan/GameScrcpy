#pragma once

#include <gs/base/Clock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gs::app {

struct MouseStep {
    int dx = 0;
    int dy = 0;
    int wheel = 0;
    std::uint32_t buttons = 0;

    friend bool operator==(const MouseStep&, const MouseStep&) = default;
};

inline constexpr int kMostMouseDelta = 32767;
inline constexpr int kMostMouseWheel = 127;
inline constexpr std::size_t kMostHeldButtons = 8;
inline constexpr Microseconds kRecenterPause{550'000};

class PointerMerge final
{
public:
    explicit PointerMerge(Microseconds tick = Microseconds{0});
    PointerMerge(Microseconds tick, const Clock& clock);

    void setTick(Microseconds tick) noexcept { m_tick = tick; }
    [[nodiscard]] Microseconds tick() const noexcept { return m_tick; }
    [[nodiscard]] std::uint32_t buttons() const noexcept { return m_buttons; }
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool holding() const noexcept;

    void addMotion(int dx, int dy, int wheel = 0);
    void setButtons(std::uint32_t buttons);
    void recenter(int x, int y);
    void park();
    void reset();

    [[nodiscard]] std::optional<MouseStep> take();

private:
    enum class Phase : std::uint8_t { Idle, Hold, Parked };

    [[nodiscard]] bool due(Microseconds now) const noexcept;
    [[nodiscard]] std::optional<MouseStep> next(Microseconds now);
    [[nodiscard]] std::optional<MouseStep> back(Microseconds now);
    [[nodiscard]] std::optional<MouseStep> popHeld(Microseconds now);
    [[nodiscard]] std::uint32_t wanted() const noexcept;
    [[nodiscard]] static int delta(int value) noexcept;

    SteadyClock m_wall;
    const Clock* m_clock = nullptr;
    Microseconds m_tick{0};
    std::optional<Microseconds> m_last;
    Microseconds m_cornerAt{0};
    Phase m_phase;
    bool m_corner = false;
    int m_dx = 0;
    int m_dy = 0;
    int m_wheel = 0;
    std::uint32_t m_buttons = 0;
    std::array<std::uint32_t, kMostHeldButtons> m_held{};
    std::size_t m_heldCount = 0;
    std::size_t m_replay = 0;
    int m_targetX = 0;
    int m_targetY = 0;
};

}
