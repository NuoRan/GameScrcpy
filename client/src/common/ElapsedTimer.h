#pragma once
#include <chrono>

/**
 * @brief 轻量级计时器，替代 QElapsedTimer
 *
 * 基于 std::chrono::steady_clock，无 Qt 依赖。
 * API 与 QElapsedTimer 兼容（start / restart / elapsed / nsecsElapsed）。
 */
struct ElapsedTimer {
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_start{};
    bool m_valid = false;

    void start() { m_start = Clock::now(); m_valid = true; }
    void restart() { start(); }
    bool isValid() const { return m_valid; }

    /// 返回自 start() 以来的毫秒数
    int64_t elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_start).count();
    }

    /// 返回自 start() 以来的纳秒数
    int64_t nsecsElapsed() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_start).count();
    }

    /// 返回自 start() 以来的微秒数
    int64_t usElapsed() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - m_start).count();
    }
};
