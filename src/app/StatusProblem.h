#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace gs::app {

enum class StatusProblem : std::uint8_t { None, SlowDelay, DroppingFrames, Reconnecting };

inline constexpr double kSlowDelayMs = 100;
inline constexpr double kMovingFramesPerSecond = 20;
inline constexpr double kShownShare = 0.7;
inline constexpr int kProblemSamples = 2;

[[nodiscard]] constexpr StatusProblem measuredProblem(std::optional<double> inputToRenderMs, double receivedPerSecond,
                                                      double shownPerSecond, double refreshRate) noexcept
{
    if (inputToRenderMs && *inputToRenderMs > kSlowDelayMs) {
        return StatusProblem::SlowDelay;
    }
    if (receivedPerSecond >= kMovingFramesPerSecond) {
        const double showable = refreshRate > 0 ? std::min(receivedPerSecond, refreshRate) : receivedPerSecond;
        if (shownPerSecond < kShownShare * showable) {
            return StatusProblem::DroppingFrames;
        }
    }
    return StatusProblem::None;
}

class ProblemFilter
{
public:
    StatusProblem update(StatusProblem current) noexcept
    {
        if (current == StatusProblem::None) {
            m_candidate = StatusProblem::None;
            m_streak = 0;
            m_shown = StatusProblem::None;
        } else if (current == m_candidate) {
            m_streak = std::min(m_streak + 1, kProblemSamples);
        } else {
            m_candidate = current;
            m_streak = 1;
        }
        if (m_streak >= kProblemSamples) {
            m_shown = m_candidate;
        }
        return m_shown;
    }

    [[nodiscard]] StatusProblem shown() const noexcept { return m_shown; }

private:
    StatusProblem m_candidate = StatusProblem::None;
    StatusProblem m_shown = StatusProblem::None;
    int m_streak = 0;
};

}
