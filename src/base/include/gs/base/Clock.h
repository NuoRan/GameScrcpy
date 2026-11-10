#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace gs {

using Microseconds = std::chrono::microseconds;

class Clock
{
public:
    virtual ~Clock() = default;

    [[nodiscard]] virtual Microseconds now() const noexcept = 0;
};

class SteadyClock final : public Clock
{
public:
    [[nodiscard]] Microseconds now() const noexcept override
    {
        return std::chrono::duration_cast<Microseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }
};

class ManualClock final : public Clock
{
public:
    explicit ManualClock(Microseconds start = Microseconds{0}) noexcept
        : m_now(start.count())
    {
    }

    [[nodiscard]] Microseconds now() const noexcept override
    {
        return Microseconds{m_now.load(std::memory_order_acquire)};
    }

    void advance(Microseconds delta) noexcept
    {
        m_now.fetch_add(delta.count(), std::memory_order_acq_rel);
    }

    void set(Microseconds time) noexcept { m_now.store(time.count(), std::memory_order_release); }

private:
    std::atomic<std::int64_t> m_now;
};

}
