#include "PointerMerge.h"

#include <algorithm>
#include <utility>

namespace gs::app {

PointerMerge::PointerMerge(Microseconds tick)
    : m_tick(tick)
    , m_phase(Phase::Idle)
{
}

PointerMerge::PointerMerge(Microseconds tick, const Clock& clock)
    : m_clock(&clock)
    , m_tick(tick)
    , m_phase(Phase::Idle)
{
}

bool PointerMerge::holding() const noexcept
{
    return m_phase != Phase::Idle;
}

int PointerMerge::delta(int value) noexcept
{
    return std::clamp(value, -kMostMouseDelta, kMostMouseDelta);
}

bool PointerMerge::busy() const noexcept
{
    switch (m_phase) {
    case Phase::Hold:
        return true;
    case Phase::Parked:
        return m_corner;
    case Phase::Idle:
        break;
    }
    return m_dx != 0 || m_dy != 0 || m_wheel != 0 || m_heldCount > 0;
}

bool PointerMerge::due(Microseconds now) const noexcept
{
    if (m_heldCount > 0) {
        return true;
    }
    if (m_tick <= Microseconds{0}) {
        return true;
    }
    return !m_last.has_value() || now - *m_last >= m_tick;
}

void PointerMerge::addMotion(int dx, int dy, int wheel)
{
    if (dx == 0 && dy == 0 && wheel == 0) {
        return;
    }
    m_dx += dx;
    m_dy += dy;
    m_wheel += wheel;
}

void PointerMerge::setButtons(std::uint32_t buttons)
{
    if (buttons == wanted()) {
        return;
    }
    if (m_heldCount < m_held.size()) {
        m_held[m_heldCount] = buttons;
        ++m_heldCount;
        return;
    }
    m_held.back() = buttons;
}

std::uint32_t PointerMerge::wanted() const noexcept
{
    return m_heldCount > 0 ? m_held[m_heldCount - 1] : m_buttons;
}

void PointerMerge::recenter(int x, int y)
{
    if (m_phase != Phase::Parked) {
        m_corner = true;
    }
    m_phase = Phase::Hold;
    m_targetX = x;
    m_targetY = y;
    m_dx = 0;
    m_dy = 0;
}

void PointerMerge::park()
{
    const std::uint32_t last = wanted();
    m_heldCount = 0;
    m_replay = 0;
    if (last != m_buttons) {
        m_held[0] = last;
        m_heldCount = 1;
    }
    m_phase = Phase::Parked;
    m_corner = true;
    m_dx = 0;
    m_dy = 0;
    m_wheel = 0;
    m_targetX = 0;
    m_targetY = 0;
}

void PointerMerge::reset()
{
    m_phase = Phase::Idle;
    m_corner = false;
    m_last.reset();
    m_cornerAt = Microseconds{0};
    m_dx = 0;
    m_dy = 0;
    m_wheel = 0;
    m_buttons = 0;
    m_heldCount = 0;
    m_replay = 0;
    m_targetX = 0;
    m_targetY = 0;
}

std::optional<MouseStep> PointerMerge::take()
{
    return next(m_clock != nullptr ? m_clock->now() : m_wall.now());
}

std::optional<MouseStep> PointerMerge::next(Microseconds now)
{
    if (m_corner) {
        m_corner = false;
        m_cornerAt = now;
        m_last = now;
        return MouseStep{.dx = -kMostMouseDelta, .dy = -kMostMouseDelta, .wheel = 0, .buttons = m_buttons};
    }
    if (m_phase == Phase::Parked) {
        return std::nullopt;
    }
    if (m_phase == Phase::Hold) {
        if (now < m_cornerAt + kRecenterPause) {
            return std::nullopt;
        }
        m_phase = Phase::Idle;
        m_replay = m_heldCount;
        if (std::optional<MouseStep> step = back(now)) {
            return step;
        }
    }
    while (m_replay > 0) {
        --m_replay;
        if (std::optional<MouseStep> held = popHeld(now)) {
            return held;
        }
    }
    if (!due(now)) {
        return std::nullopt;
    }
    if (m_dx != 0 || m_dy != 0 || m_wheel != 0) {
        const MouseStep step{.dx = delta(m_dx),
                             .dy = delta(m_dy),
                             .wheel = std::clamp(m_wheel, -kMostMouseWheel, kMostMouseWheel),
                             .buttons = m_buttons};
        m_dx -= step.dx;
        m_dy -= step.dy;
        m_wheel -= step.wheel;
        m_last = now;
        return step;
    }
    while (m_heldCount > 0) {
        if (std::optional<MouseStep> held = popHeld(now)) {
            return held;
        }
    }
    return std::nullopt;
}

std::optional<MouseStep> PointerMerge::back(Microseconds now)
{
    const int targetX = std::exchange(m_targetX, 0) + std::exchange(m_dx, 0);
    const int targetY = std::exchange(m_targetY, 0) + std::exchange(m_dy, 0);
    if (targetX == 0 && targetY == 0) {
        return std::nullopt;
    }
    const MouseStep step{.dx = delta(targetX), .dy = delta(targetY), .wheel = 0, .buttons = m_buttons};
    m_dx = targetX - step.dx;
    m_dy = targetY - step.dy;
    m_last = now;
    return step;
}

std::optional<MouseStep> PointerMerge::popHeld(Microseconds now)
{
    if (m_heldCount == 0) {
        return std::nullopt;
    }
    const std::uint32_t first = m_held[0];
    for (std::size_t i = 1; i < m_heldCount; ++i) {
        m_held[i - 1] = m_held[i];
    }
    --m_heldCount;
    if (first == m_buttons) {
        return std::nullopt;
    }
    m_buttons = first;
    m_last = now;
    return MouseStep{.dx = 0, .dy = 0, .wheel = 0, .buttons = first};
}

}
