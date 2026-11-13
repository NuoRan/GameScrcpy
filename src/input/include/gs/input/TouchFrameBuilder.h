#pragma once

#include <gs/base/Clock.h>
#include <gs/input/Geometry.h>
#include <gs/protocol/Messages.h>

#include <cstdint>
#include <deque>
#include <optional>

namespace gs::input {

[[nodiscard]] Microseconds pacingTickFor(double refreshHz) noexcept;

class TouchFrameSink
{
public:
    virtual ~TouchFrameSink() = default;

    [[nodiscard]] virtual bool trySend(const protocol::TouchFrame& frame) = 0;
    virtual void frameWaited(Microseconds) {}
};

class TouchFrameBuilder
{
public:
    explicit TouchFrameBuilder(Microseconds tick) noexcept;
    TouchFrameBuilder(Microseconds tick, const Clock& clock) noexcept;

    void setTick(Microseconds tick) noexcept { m_tick = tick; }
    void setVideoSession(std::uint16_t session) noexcept { m_videoSession = session; }

    void down(std::uint8_t pointerId, Vec2 position);
    void move(std::uint8_t pointerId, Vec2 position);
    void up(std::uint8_t pointerId, Vec2 position);
    void cancel(std::uint8_t pointerId, Vec2 position);

    void flush(Microseconds now, TouchFrameSink& sink);

    [[nodiscard]] std::optional<Microseconds> nextWakeup() const noexcept;

    [[nodiscard]] bool idle() const noexcept { return m_ready.empty() && m_open.points.empty(); }

private:
    struct Queued {
        protocol::TouchFrame frame;
        Microseconds since{0};
    };

    [[nodiscard]] Microseconds stamp() const noexcept;
    void add(protocol::TouchAction action, std::uint8_t pointerId, Vec2 position);
    void closeOpenFrame();
    [[nodiscard]] bool openFrameHasOnlyMoves() const noexcept;

    SteadyClock m_wall;
    const Clock* m_clock = nullptr;
    Microseconds m_tick;
    std::uint16_t m_videoSession = 0;
    protocol::TouchFrame m_open;
    Microseconds m_openSince{0};
    std::deque<Queued> m_ready;
    std::optional<Microseconds> m_lastSent;
    bool m_blocked = false;
    Microseconds m_blockedSince{0};
};

}
