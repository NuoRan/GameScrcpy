#include "gs/input/TouchFrameBuilder.h"

#include <algorithm>
#include <cmath>

namespace gs::input {

namespace {

using protocol::TouchAction;
using protocol::TouchPoint;

constexpr std::uint16_t kFullPressure = 0xFFFF;

TouchPoint* findPoint(protocol::TouchFrame& frame, std::uint8_t pointerId) noexcept
{
    for (TouchPoint& point : frame.points) {
        if (point.pointerId == pointerId) {
            return &point;
        }
    }
    return nullptr;
}

bool merge(TouchPoint& existing, const TouchPoint& next) noexcept
{
    const bool existingCarriesPosition = existing.action == TouchAction::Down || existing.action == TouchAction::Move;
    if (!existingCarriesPosition) {
        return false;
    }
    if (next.action == TouchAction::Move) {
        existing.x = next.x;
        existing.y = next.y;
        return true;
    }
    if (existing.action == TouchAction::Move && (next.action == TouchAction::Up || next.action == TouchAction::Cancel)) {
        existing = next;
        return true;
    }
    return false;
}

}

Microseconds pacingTickFor(double refreshHz) noexcept
{
    const double hz = std::clamp(2.0 * refreshHz, 120.0, 480.0);
    return Microseconds{static_cast<long long>(1'000'000.0 / hz)};
}

TouchFrameBuilder::TouchFrameBuilder(Microseconds tick) noexcept
    : m_tick(tick)
{
}

TouchFrameBuilder::TouchFrameBuilder(Microseconds tick, const Clock& clock) noexcept
    : m_clock(&clock)
    , m_tick(tick)
{
}

Microseconds TouchFrameBuilder::stamp() const noexcept
{
    return m_clock != nullptr ? m_clock->now() : m_wall.now();
}

void TouchFrameBuilder::down(std::uint8_t pointerId, Vec2 position)
{
    add(TouchAction::Down, pointerId, position);
}

void TouchFrameBuilder::move(std::uint8_t pointerId, Vec2 position)
{
    add(TouchAction::Move, pointerId, position);
}

void TouchFrameBuilder::up(std::uint8_t pointerId, Vec2 position)
{
    add(TouchAction::Up, pointerId, position);
}

void TouchFrameBuilder::cancel(std::uint8_t pointerId, Vec2 position)
{
    add(TouchAction::Cancel, pointerId, position);
}

void TouchFrameBuilder::add(TouchAction action, std::uint8_t pointerId, Vec2 position)
{
    const bool lifting = action == TouchAction::Up || action == TouchAction::Cancel;
    const TouchPoint next{.action = action,
                          .pointerId = pointerId,
                          .x = fixedCoordinate(position.x),
                          .y = fixedCoordinate(position.y),
                          .pressure = lifting ? std::uint16_t{0} : kFullPressure};
    if (TouchPoint* existing = findPoint(m_open, pointerId)) {
        if (merge(*existing, next)) {
            return;
        }
        closeOpenFrame();
    }
    if (m_open.points.empty()) {
        m_openSince = stamp();
    }
    m_open.points.push_back(next);
}

void TouchFrameBuilder::closeOpenFrame()
{
    if (m_open.points.empty()) {
        return;
    }
    m_open.videoSession = m_videoSession;
    m_ready.push_back({.frame = m_open, .since = m_openSince});
    m_open.points.clear();
}

bool TouchFrameBuilder::openFrameHasOnlyMoves() const noexcept
{
    return std::all_of(m_open.points.begin(), m_open.points.end(),
                       [](const TouchPoint& point) { return point.action == TouchAction::Move; });
}

void TouchFrameBuilder::flush(Microseconds now, TouchFrameSink& sink)
{
    m_blocked = false;
    if (!m_open.points.empty()) {
        const bool tickElapsed = m_tick.count() == 0 || !m_lastSent || now - *m_lastSent >= m_tick;
        if (!openFrameHasOnlyMoves() || tickElapsed) {
            closeOpenFrame();
        }
    }
    while (!m_ready.empty()) {
        Queued& queued = m_ready.front();
        if (!sink.trySend(queued.frame)) {
            m_blocked = true;
            if (m_ready.size() == 1 && m_open.points.empty()) {
                m_open = std::move(queued.frame);
                m_openSince = queued.since;
                m_ready.pop_front();
            }
            return;
        }
        sink.frameWaited(now > queued.since ? now - queued.since : Microseconds{0});
        m_ready.pop_front();
        m_lastSent = now;
    }
}

std::optional<Microseconds> TouchFrameBuilder::nextWakeup() const noexcept
{
    if (m_blocked) {
        return std::nullopt;
    }
    if (!m_ready.empty() || (!m_open.points.empty() && !openFrameHasOnlyMoves())) {
        return Microseconds{0};
    }
    if (m_open.points.empty()) {
        return std::nullopt;
    }
    return m_lastSent ? *m_lastSent + m_tick : Microseconds{0};
}

}
