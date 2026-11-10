#include "gs/input/Controls.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace gs::input {

namespace {

constexpr Microseconds kClickWalk{200'000};
constexpr double kStickCountsPerSecond = 1500.0;
constexpr Microseconds kModuleSoonest{1'000};

Vec2 add(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
Vec2 subtract(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
Vec2 scale(Vec2 v, double factor) noexcept { return {v.x * factor, v.y * factor}; }
bool isZero(Vec2 v) noexcept { return v.x == 0.0 && v.y == 0.0; }

Vec2 clampToUnitCircle(Vec2 v) noexcept
{
    const double length = std::hypot(v.x, v.y);
    return length > 1.0 ? scale(v, 1.0 / length) : v;
}

}

std::optional<std::uint8_t> TouchOutput::down(Vec2 position)
{
    if (m_route != nullptr) {
        return m_route->down(position);
    }
    const auto pointer = m_pointers.acquire();
    if (pointer) {
        m_frames.down(*pointer, position);
    }
    return pointer;
}

void TouchOutput::move(std::uint8_t pointer, Vec2 position)
{
    if (m_route != nullptr) {
        m_route->move(pointer, position);
        return;
    }
    m_frames.move(pointer, position);
}

void TouchOutput::up(std::uint8_t pointer, Vec2 position)
{
    if (m_route != nullptr) {
        m_route->up(pointer, position);
        return;
    }
    m_frames.up(pointer, position);
    m_pointers.release(pointer);
}

void TouchOutput::cancel(std::uint8_t pointer, Vec2 position)
{
    if (m_route != nullptr) {
        m_route->cancel(pointer, position);
        return;
    }
    m_frames.cancel(pointer, position);
    m_pointers.release(pointer);
}

void ButtonComponent::signal(Slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    switch (signal) {
    case TriggerSignal::Activate:
        m_releaseAt.reset();
        if (!m_pointer) {
            press(out);
        }
        break;
    case TriggerSignal::Deactivate:
        if (m_pointer && !m_releaseAt) {
            lift(out);
        }
        break;
    case TriggerSignal::Pulse:
        if (m_pointer) {
            lift(out);
        }
        press(out);
        if (m_pointer) {
            m_releaseAt = now + m_config.tapDuration;
        }
        break;
    case TriggerSignal::None:
        break;
    }
}

void ButtonComponent::tick(Microseconds now, TouchOutput& out)
{
    if (m_releaseAt && now >= *m_releaseAt) {
        m_releaseAt.reset();
        if (m_pointer) {
            lift(out);
        }
    }
}

void ButtonComponent::reset(TouchOutput& out)
{
    if (m_pointer) {
        out.cancel(*m_pointer, m_at);
        m_pointer.reset();
    }
    m_releaseAt.reset();
}

void ButtonComponent::press(TouchOutput& out)
{
    m_at = m_config.position;
    m_pointer = out.down(m_at);
}

void ButtonComponent::lift(TouchOutput& out)
{
    out.up(*m_pointer, m_at);
    m_pointer.reset();
}

void JoystickComponent::signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    if (signal != TriggerSignal::Activate && signal != TriggerSignal::Deactivate) {
        return;
    }
    const bool on = signal == TriggerSignal::Activate;
    switch (slot) {
    case Slot::Up: m_up = on; break;
    case Slot::Down: m_down = on; break;
    case Slot::Left: m_left = on; break;
    case Slot::Right: m_right = on; break;
    case Slot::Walk: m_walk = on; break;
    case Slot::Sprint: m_sprint = on; break;
    case Slot::WalkTo:
        if (on) {
            m_walkHeld = walkTowardCursor(now);
            m_walkPressed = now;
        } else if (m_walkHeld) {
            m_walkHeld = false;
            if (now - m_walkPressed >= kClickWalk) {
                if (m_config.stopOnRelease) {
                    stopWalking();
                } else {
                    walkTowardCursor(now);
                }
            }
        }
        break;
    case Slot::Stop:
        if (on) {
            stopWalking();
        }
        break;
    default: return;
    }
    if (on && (slot == Slot::Up || slot == Slot::Down || slot == Slot::Left || slot == Slot::Right)) {
        stopWalking();
    }
    update(now, out);
}

void JoystickComponent::follow(Microseconds now, TouchOutput& out)
{
    if (m_walkHeld && walkTowardCursor(now)) {
        update(now, out);
    }
}

bool JoystickComponent::walkTowardCursor(Microseconds now)
{
    const std::optional<Vec2> cursor =
        m_config.locate != nullptr && *m_config.locate ? (*m_config.locate)() : std::nullopt;
    return cursor && aimWalk(*cursor, now);
}

bool JoystickComponent::aimWalk(Vec2 point, Microseconds now)
{
    const Vec2 offset{(point.x - m_config.hero.x) / m_config.unit.x, (point.y - m_config.hero.y) / m_config.unit.y};
    const double distance = std::hypot(offset.x, offset.y);
    if (distance <= 0.0) {
        return false;
    }
    m_walkDirection = scale(offset, 1.0 / distance);
    m_walking = true;
    m_walkUntil = now + Microseconds{static_cast<Microseconds::rep>(
                            distance * static_cast<double>(m_config.walkPerUnit.count()))};
    return true;
}

void JoystickComponent::walkToward(Vec2 point, Microseconds now, TouchOutput& out)
{
    if (aimWalk(point, now)) {
        m_walkHeld = false;
        update(now, out);
    }
}

void JoystickComponent::stopWalking() noexcept
{
    m_walking = false;
    m_walkHeld = false;
    m_walkUntil.reset();
}

void JoystickComponent::setFrozen(bool frozen, Microseconds now, TouchOutput& out)
{
    if (frozen == m_frozen) {
        return;
    }
    m_frozen = frozen;
    if (!frozen) {
        update(now, out);
    }
}

std::optional<Microseconds> JoystickComponent::nextDeadline() const noexcept
{
    const std::optional<Microseconds> walkEnd = m_walking && !m_walkHeld ? m_walkUntil : std::nullopt;
    if (!m_deadline) {
        return walkEnd;
    }
    return walkEnd ? std::min(*m_deadline, *walkEnd) : m_deadline;
}

void JoystickComponent::axis(Slot slot, Vec2 value, Microseconds now, TouchOutput& out)
{
    if (slot != Slot::Stick) {
        return;
    }
    m_stick = clampToUnitCircle(value);
    update(now, out);
}

Vec2 JoystickComponent::target() const noexcept
{
    Vec2 direction{(m_right ? 1.0 : 0.0) - (m_left ? 1.0 : 0.0), (m_down ? 1.0 : 0.0) - (m_up ? 1.0 : 0.0)};
    const double length = std::hypot(direction.x, direction.y);
    direction = length > 0.0 ? scale(direction, 1.0 / length) : m_walking ? m_walkDirection : m_stick;
    const double factor = m_sprint ? m_config.sprintScale : (m_walk ? m_config.walkScale : 1.0);
    return scale(direction, factor);
}

void JoystickComponent::update(Microseconds now, TouchOutput& out)
{
    if (m_frozen) {
        return;
    }
    const Vec2 goal = target();
    if (isZero(goal)) {
        if (m_pointer) {
            out.up(*m_pointer, add(m_landing, {m_offset.x * m_config.radius.x, m_offset.y * m_config.radius.y}));
            m_pointer.reset();
        }
        m_landed = false;
        m_offset = {};
        m_deadline.reset();
        return;
    }
    if (!m_pointer) {
        m_landing = m_config.center;
        m_pointer = out.down(m_landing);
        if (m_pointer) {
            m_offset = {};
            m_landed = true;
            m_deadline = now;
        }
        return;
    }
    if (!m_landed) {
        moveTo(goal, out);
    }
}

void JoystickComponent::tick(Microseconds now, TouchOutput& out)
{
    if (m_walking && !m_walkHeld && m_walkUntil && now >= *m_walkUntil) {
        stopWalking();
        update(now, out);
    }
    if (!m_deadline || now < *m_deadline || !m_pointer) {
        return;
    }
    m_deadline.reset();
    if (m_landed) {
        m_landed = false;
        if (!m_frozen) {
            moveTo(target(), out);
        }
    }
}

void JoystickComponent::reset(TouchOutput& out)
{
    if (m_pointer) {
        out.cancel(*m_pointer, add(m_landing, {m_offset.x * m_config.radius.x, m_offset.y * m_config.radius.y}));
        m_pointer.reset();
    }
    m_up = m_down = m_left = m_right = m_walk = m_sprint = false;
    m_stick = {};
    m_landed = false;
    m_offset = {};
    m_deadline.reset();
    stopWalking();
    m_frozen = false;
}

void JoystickComponent::moveTo(Vec2 offset, TouchOutput& out)
{
    m_offset = offset;
    out.move(*m_pointer, add(m_landing, {offset.x * m_config.radius.x, offset.y * m_config.radius.y}));
}

void CameraComponent::motion(Slot slot, double dx, double dy, Microseconds now, TouchOutput& out)
{
    if (slot != Slot::Look) {
        return;
    }
    m_pending = add(m_pending, {dx * m_config.perCount.x, dy * m_config.perCount.y});
    m_lastMotion = now;
    switch (m_phase) {
    case Phase::Idle:
        m_pointer = out.down(m_config.anchor);
        if (!m_pointer) {
            m_pending = {};
            return;
        }
        m_at = m_config.anchor;
        m_phase = Phase::Landed;
        m_followUp = now;
        break;
    case Phase::Tracking:
        apply(out);
        if (m_phase == Phase::HandingOff) {
            m_followUp = now;
        }
        break;
    case Phase::Landed:
    case Phase::HandingOff:
        break;
    }
}

void CameraComponent::axis(Slot slot, Vec2 value, Microseconds now, TouchOutput& out)
{
    if (slot != Slot::Look) {
        return;
    }
    const bool turning = !isZero(m_velocity);
    m_velocity = value;
    if (isZero(value)) {
        m_stepAt.reset();
        return;
    }
    if (!turning) {
        m_lastStep = now;
        m_stepAt = now + m_config.frame;
        motion(Slot::Look, 0.0, 0.0, now, out);
    }
}

void CameraComponent::tick(Microseconds now, TouchOutput& out)
{
    if (m_stepAt && now >= *m_stepAt) {
        const double seconds = static_cast<double>((now - m_lastStep).count()) / 1'000'000.0;
        m_lastStep = now;
        m_stepAt = now + m_config.frame;
        motion(Slot::Look, m_velocity.x * kStickCountsPerSecond * seconds,
               m_velocity.y * kStickCountsPerSecond * seconds, now, out);
    }
    switch (m_phase) {
    case Phase::Landed:
        if (m_followUp && now >= *m_followUp) {
            m_followUp.reset();
            m_phase = Phase::Tracking;
            apply(out);
            if (m_phase == Phase::HandingOff) {
                m_followUp = now;
            }
        }
        break;
    case Phase::HandingOff:
        if (m_followUp && now >= *m_followUp) {
            out.up(*m_retiring, m_retiringAt);
            m_retiring.reset();
            m_phase = Phase::Landed;
            m_followUp = now;
        }
        break;
    case Phase::Tracking:
        if (m_config.idleRecenter.count() > 0 && !centered() && now - m_lastMotion >= m_config.idleRecenter) {
            handOver(m_at, now, out);
        }
        break;
    case Phase::Idle:
        break;
    }
}

std::optional<Microseconds> CameraComponent::nextDeadline() const noexcept
{
    const std::optional<Microseconds> phase = phaseDeadline();
    if (!m_stepAt) {
        return phase;
    }
    return phase ? std::min(*phase, *m_stepAt) : m_stepAt;
}

std::optional<Microseconds> CameraComponent::phaseDeadline() const noexcept
{
    switch (m_phase) {
    case Phase::Landed:
    case Phase::HandingOff:
        return m_followUp;
    case Phase::Tracking:
        if (m_config.idleRecenter.count() > 0 && !centered()) {
            return m_lastMotion + m_config.idleRecenter;
        }
        return std::nullopt;
    case Phase::Idle:
        return std::nullopt;
    }
    return std::nullopt;
}

void CameraComponent::reset(TouchOutput& out)
{
    if (m_pointer) {
        out.cancel(*m_pointer, m_at);
        m_pointer.reset();
    }
    if (m_retiring) {
        out.cancel(*m_retiring, m_retiringAt);
        m_retiring.reset();
    }
    m_pending = {};
    m_phase = Phase::Idle;
    m_followUp.reset();
    m_velocity = {};
    m_stepAt.reset();
}

void CameraComponent::apply(TouchOutput& out)
{
    if (isZero(m_pending)) {
        return;
    }
    const Vec2 low = this->low();
    const Vec2 high = this->high();
    const Vec2 target = add(m_at, m_pending);

    double fits = 1.0;
    const auto limit = [&](double from, double to, double lo, double hi) {
        if (to > hi && to != from) {
            fits = std::min(fits, (hi - from) / (to - from));
        } else if (to < lo && to != from) {
            fits = std::min(fits, (lo - from) / (to - from));
        }
    };
    limit(m_at.x, target.x, low.x, high.x);
    limit(m_at.y, target.y, low.y, high.y);
    fits = std::max(fits, 0.0);

    if (fits >= 1.0) {
        m_at = target;
        m_pending = {};
        out.move(*m_pointer, m_at);
        return;
    }
    const Vec2 edge = add(m_at, scale(m_pending, fits));
    if (fits > 0.0) {
        out.move(*m_pointer, edge);
    }
    const auto next = out.down(m_config.anchor);
    if (!next) {
        m_at = edge;
        m_pending = {};
        return;
    }
    m_pending = subtract(target, edge);
    m_retiring = m_pointer;
    m_retiringAt = edge;
    m_pointer = next;
    m_at = m_config.anchor;
    m_phase = Phase::HandingOff;
}

void CameraComponent::handOver(Vec2 from, Microseconds now, TouchOutput& out)
{
    const auto next = out.down(m_config.anchor);
    if (!next) {
        return;
    }
    m_retiring = m_pointer;
    m_retiringAt = from;
    m_pointer = next;
    m_at = m_config.anchor;
    m_phase = Phase::HandingOff;
    m_followUp = now;
}

bool CameraComponent::centered() const noexcept
{
    return m_at.x == m_config.anchor.x && m_at.y == m_config.anchor.y;
}

Vec2 CameraComponent::low() const noexcept
{
    constexpr double kEdge = 0.02;
    return m_config.wholePicture ? Vec2{std::min(kEdge, m_config.anchor.x), std::min(kEdge, m_config.anchor.y)}
                                 : subtract(m_config.anchor, m_config.halfExtent);
}

Vec2 CameraComponent::high() const noexcept
{
    constexpr double kEdge = 0.98;
    return m_config.wholePicture ? Vec2{std::max(kEdge, m_config.anchor.x), std::max(kEdge, m_config.anchor.y)}
                                 : add(m_config.anchor, m_config.halfExtent);
}

void FreeLookComponent::signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    if (slot != Slot::Hold) {
        return;
    }
    if (signal == TriggerSignal::Activate || signal == TriggerSignal::Pulse) {
        if (m_pointer && !m_held) {
            out.up(*m_pointer, m_at);
            m_pointer.reset();
            return;
        }
        if (!m_pointer) {
            m_at = m_config.position;
            m_pointer = out.down(m_at);
            m_held = m_pointer.has_value();
            m_pending = {};
            m_followUp = m_held ? std::optional{now} : std::nullopt;
        }
    } else if (signal == TriggerSignal::Deactivate) {
        m_held = false;
        m_followUp.reset();
        if (m_pointer && m_config.resetOnRelease) {
            out.up(*m_pointer, m_at);
            m_pointer.reset();
        }
    }
}

void FreeLookComponent::motion(Slot slot, double dx, double dy, Microseconds, TouchOutput& out)
{
    if (slot != Slot::Look || !m_held || !m_pointer) {
        return;
    }
    m_pending = add(m_pending, {dx * m_config.perCount.x, dy * m_config.perCount.y});
    if (!m_followUp) {
        m_at = add(m_at, m_pending);
        m_pending = {};
        out.move(*m_pointer, m_at);
    }
}

void FreeLookComponent::tick(Microseconds now, TouchOutput& out)
{
    if (!m_followUp || now < *m_followUp) {
        return;
    }
    m_followUp.reset();
    if (m_pointer && !isZero(m_pending)) {
        m_at = add(m_at, m_pending);
        m_pending = {};
        out.move(*m_pointer, m_at);
    }
}

void FreeLookComponent::reset(TouchOutput& out)
{
    if (m_pointer) {
        out.cancel(*m_pointer, m_at);
        m_pointer.reset();
    }
    m_held = false;
    m_pending = {};
    m_followUp.reset();
}

void AimComponent::signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    const bool pressing = signal == TriggerSignal::Activate || signal == TriggerSignal::Pulse;
    if (slot == Slot::Cancel) {
        if (pressing && m_pointer && !m_cancelling) {
            cancelCast(now, out);
        }
        return;
    }
    if (slot == Slot::Cast) {
        if (pressing) {
            castByClick(out);
        }
        return;
    }
    if (slot != Slot::Press) {
        return;
    }
    if (pressing && m_pointer) {
        if (m_config.cast == CastTiming::Manual && !m_cancelling) {
            castByClick(out);
        }
        return;
    }
    if (pressing) {
        m_cancelled = false;
        m_offset = m_config.cursor ? cursorOffset().value_or(Vec2{}) : Vec2{};
        m_pointer = out.down(m_config.position);
        if (m_pointer) {
            m_landed = true;
            m_releaseAfterLanding = signal == TriggerSignal::Pulse || m_config.cast == CastTiming::Press;
            m_followUp = now;
        }
    } else if (signal == TriggerSignal::Deactivate && m_pointer && m_config.cast == CastTiming::Release &&
               !m_cancelling) {
        if (m_landed) {
            m_releaseAfterLanding = true;
        } else {
            release(out);
        }
    }
}

void AimComponent::follow(Microseconds, TouchOutput& out)
{
    if (!m_config.cursor || !engaged() || m_cancelling) {
        return;
    }
    if (const std::optional<Vec2> offset = cursorOffset()) {
        m_offset = *offset;
        if (!m_landed) {
            out.move(*m_pointer, position());
        }
    }
}

bool AimComponent::castByClick(TouchOutput& out)
{
    if (!armed()) {
        return false;
    }
    if (m_landed) {
        m_releaseAfterLanding = true;
    } else {
        cast(out);
    }
    return true;
}

void AimComponent::cancelCast(Microseconds now, TouchOutput& out)
{
    m_cancelled = true;
    if (m_config.cancelAt) {
        out.move(*m_pointer, *m_config.cancelAt);
        m_cancelling = true;
        m_followUp = now + m_config.frame;
        return;
    }
    out.cancel(*m_pointer, position());
    clear();
}

std::optional<Vec2> AimComponent::cursorOffset() const
{
    const std::optional<Vec2> cursor =
        m_config.locate != nullptr && *m_config.locate ? (*m_config.locate)() : std::nullopt;
    if (!cursor) {
        return std::nullopt;
    }
    const Vec2 unit = clampToUnitCircle(
        {(cursor->x - m_config.hero.x) / m_config.castExtent.x, (cursor->y - m_config.hero.y) / m_config.castExtent.y});
    return Vec2{unit.x * m_config.radius.x, unit.y * m_config.radius.y};
}

void AimComponent::motion(Slot slot, double dx, double dy, Microseconds, TouchOutput& out)
{
    if (slot != Slot::Look || !engaged() || m_config.cursor) {
        return;
    }
    Vec2 unit{(m_offset.x + dx * m_config.perCount.x) / m_config.radius.x,
              (m_offset.y + dy * m_config.perCount.y) / m_config.radius.y};
    unit = clampToUnitCircle(unit);
    m_offset = {unit.x * m_config.radius.x, unit.y * m_config.radius.y};
    if (!m_landed) {
        out.move(*m_pointer, position());
    }
}

void AimComponent::tick(Microseconds now, TouchOutput& out)
{
    if (!m_followUp || now < *m_followUp || !m_pointer) {
        return;
    }
    m_followUp.reset();
    if (m_cancelling) {
        out.up(*m_pointer, *m_config.cancelAt);
        clear();
        return;
    }
    m_landed = false;
    if (!isZero(m_offset)) {
        out.move(*m_pointer, position());
    }
    if (m_releaseAfterLanding) {
        m_releaseAfterLanding = false;
        if (m_config.cast == CastTiming::Release) {
            release(out);
        } else {
            cast(out);
        }
    }
}

void AimComponent::reset(TouchOutput& out)
{
    if (m_pointer) {
        m_cancelled = true;
        out.cancel(*m_pointer, m_cancelling ? *m_config.cancelAt : position());
    }
    clear();
}

void AimComponent::release(TouchOutput& out)
{
    if (m_config.castOnRelease) {
        cast(out);
        return;
    }
    out.cancel(*m_pointer, position());
    clear();
}

void AimComponent::cast(TouchOutput& out)
{
    out.up(*m_pointer, position());
    clear();
}

void AimComponent::clear() noexcept
{
    m_pointer.reset();
    m_offset = {};
    m_landed = false;
    m_releaseAfterLanding = false;
    m_cancelling = false;
    m_followUp.reset();
}

Vec2 AimComponent::position() const noexcept
{
    return add(m_config.position, m_offset);
}

void SwipeComponent::signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    if (slot != Slot::Press || m_pointer || m_config.path.size() < 2) {
        return;
    }
    if (signal == TriggerSignal::Activate || signal == TriggerSignal::Pulse) {
        m_at = m_config.path.front();
        m_pointer = out.down(m_at);
        if (m_pointer) {
            m_startedAt = now;
            m_followUp = now + m_config.frame;
        }
    }
}

void SwipeComponent::tick(Microseconds now, TouchOutput& out)
{
    if (!m_followUp || now < *m_followUp || !m_pointer) {
        return;
    }
    const double total = static_cast<double>(m_config.duration.count());
    const double progress = total > 0.0 ? std::min(1.0, static_cast<double>((now - m_startedAt).count()) / total) : 1.0;
    m_at = pointAt(progress);
    if (progress >= 1.0) {
        out.up(*m_pointer, m_at);
        m_pointer.reset();
        m_followUp.reset();
        return;
    }
    out.move(*m_pointer, m_at);
    m_followUp = std::min(now + m_config.frame, m_startedAt + m_config.duration);
}

void SwipeComponent::reset(TouchOutput& out)
{
    if (m_pointer) {
        out.cancel(*m_pointer, m_at);
        m_pointer.reset();
    }
    m_followUp.reset();
}

Vec2 SwipeComponent::pointAt(double progress) const noexcept
{
    const std::vector<Vec2>& path = m_config.path;
    double length = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        length += std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
    }
    double remaining = progress * length;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const double segment = std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
        if (remaining <= segment && segment > 0.0) {
            return add(path[i - 1], scale(subtract(path[i], path[i - 1]), remaining / segment));
        }
        remaining -= segment;
    }
    return path.back();
}

void PinchComponent::signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    if ((slot != Slot::ZoomIn && slot != Slot::ZoomOut) || m_left || m_right) {
        return;
    }
    if (signal != TriggerSignal::Activate && signal != TriggerSignal::Pulse) {
        return;
    }
    m_from = m_config.halfSpan;
    m_to = slot == Slot::ZoomIn ? m_config.halfSpan + m_config.halfStep : m_config.halfSpan - m_config.halfStep;
    m_half = m_from;
    m_left = out.down({m_config.center.x - m_half, m_config.center.y});
    m_right = out.down({m_config.center.x + m_half, m_config.center.y});
    if (!m_left || !m_right) {
        reset(out);
        return;
    }
    m_startedAt = now;
    m_followUp = now + m_config.frame;
}

void PinchComponent::tick(Microseconds now, TouchOutput& out)
{
    if (!m_followUp || now < *m_followUp || !m_left || !m_right) {
        return;
    }
    const double total = static_cast<double>(m_config.duration.count());
    const double progress = total > 0.0 ? std::min(1.0, static_cast<double>((now - m_startedAt).count()) / total) : 1.0;
    m_half = m_from + (m_to - m_from) * progress;
    if (progress >= 1.0) {
        out.up(*m_left, {m_config.center.x - m_half, m_config.center.y});
        out.up(*m_right, {m_config.center.x + m_half, m_config.center.y});
        m_left.reset();
        m_right.reset();
        m_followUp.reset();
        return;
    }
    place(m_half, out);
    m_followUp = std::min(now + m_config.frame, m_startedAt + m_config.duration);
}

void PinchComponent::reset(TouchOutput& out)
{
    if (m_left) {
        out.cancel(*m_left, {m_config.center.x - m_half, m_config.center.y});
        m_left.reset();
    }
    if (m_right) {
        out.cancel(*m_right, {m_config.center.x + m_half, m_config.center.y});
        m_right.reset();
    }
    m_followUp.reset();
}

void PinchComponent::place(double half, TouchOutput& out)
{
    out.move(*m_left, {m_config.center.x - half, m_config.center.y});
    out.move(*m_right, {m_config.center.x + half, m_config.center.y});
}

void DialComponent::signal(Slot slot, TriggerSignal signal, Microseconds, TouchOutput& out)
{
    if (slot != Slot::Press) {
        return;
    }
    if (signal == TriggerSignal::Activate || signal == TriggerSignal::Pulse) {
        if (!m_pointer) {
            m_pushX = 0.0;
            m_pushY = 0.0;
            m_cell = -1;
            m_pointer = out.down(m_config.center);
        }
    } else if (signal == TriggerSignal::Deactivate && m_pointer) {
        const Vec2 at = spotOf(m_cell);
        out.up(*std::exchange(m_pointer, std::nullopt), at);
        m_cell = -1;
        m_pushX = 0.0;
        m_pushY = 0.0;
    }
}

void DialComponent::motion(Slot slot, double dx, double dy, Microseconds, TouchOutput& out)
{
    if (slot != Slot::Look || !m_pointer) {
        return;
    }
    m_pushX += dx;
    m_pushY += dy;
    const double length = std::hypot(m_pushX, m_pushY);
    if (length > kReachCounts) {
        m_pushX *= kReachCounts / length;
        m_pushY *= kReachCounts / length;
    }
    const int cell = length >= kChooseCounts ? cellToward(m_pushX, m_pushY) : -1;
    if (cell != m_cell) {
        m_cell = cell;
        out.move(*m_pointer, spotOf(cell));
    }
}

void DialComponent::reset(TouchOutput& out)
{
    if (m_pointer) {
        const Vec2 at = spotOf(m_cell);
        out.cancel(*std::exchange(m_pointer, std::nullopt), at);
    }
    m_cell = -1;
    m_pushX = 0.0;
    m_pushY = 0.0;
}

int DialComponent::cellToward(double dx, double dy) const noexcept
{
    const int cells = std::max<int>(m_config.cells, 1);
    const double step = 360.0 / cells;
    const double degrees = std::atan2(dx, -dy) * 180.0 / std::numbers::pi - m_config.firstDeg;
    const double turned = std::fmod(std::fmod(degrees, 360.0) + 360.0 + step / 2.0, 360.0);
    return std::clamp(static_cast<int>(turned / step), 0, cells - 1);
}

Vec2 DialComponent::spotOf(int cell) const noexcept
{
    if (cell < 0) {
        return m_config.center;
    }
    const double step = 360.0 / std::max<int>(m_config.cells, 1);
    const double radians = (m_config.firstDeg + step * cell) * std::numbers::pi / 180.0;
    return {m_config.center.x + m_config.radius.x * std::sin(radians),
            m_config.center.y - m_config.radius.y * std::cos(radians)};
}

void MultiPressComponent::signal(Slot slot, TriggerSignal signal, Microseconds, TouchOutput& out)
{
    if (slot != Slot::Press) {
        return;
    }
    if (signal == TriggerSignal::Activate || signal == TriggerSignal::Pulse) {
        if (!m_fingers.empty()) {
            return;
        }
        for (const Vec2& spot : m_config.spots) {
            if (const std::optional<std::uint8_t> pointer = out.down(spot)) {
                m_fingers.emplace_back(*pointer, spot);
            }
        }
    } else if (signal == TriggerSignal::Deactivate) {
        lift(out);
    }
}

void MultiPressComponent::reset(TouchOutput& out)
{
    for (const auto& [pointer, at] : m_fingers) {
        out.cancel(pointer, at);
    }
    m_fingers.clear();
}

void MultiPressComponent::lift(TouchOutput& out)
{
    for (const auto& [pointer, at] : m_fingers) {
        out.up(pointer, at);
    }
    m_fingers.clear();
}

void PeekComponent::signal(Slot slot, TriggerSignal signal, Microseconds, TouchOutput&)
{
    if (slot != Slot::Press) {
        return;
    }
    if (signal == TriggerSignal::Activate || signal == TriggerSignal::Pulse) {
        m_held = true;
    } else if (signal == TriggerSignal::Deactivate) {
        m_held = false;
    }
}

void ActionWheelComponent::signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    if (slot != Slot::Press) {
        return;
    }
    if (signal == TriggerSignal::Activate || signal == TriggerSignal::Pulse) {
        if (!m_open) {
            m_open = true;
            m_pushX = 0.0;
            m_pushY = 0.0;
            m_cell = -1;
        }
    } else if (signal == TriggerSignal::Deactivate && m_open) {
        close(true, now, out);
    }
}

void ActionWheelComponent::motion(Slot slot, double dx, double dy, Microseconds, TouchOutput&)
{
    if (slot != Slot::Look || !m_open) {
        return;
    }
    m_pushX += dx;
    m_pushY += dy;
    const double length = std::hypot(m_pushX, m_pushY);
    if (length > DialComponent::kReachCounts) {
        m_pushX *= DialComponent::kReachCounts / length;
        m_pushY *= DialComponent::kReachCounts / length;
    }
    m_cell = length >= DialComponent::kChooseCounts ? cellToward(m_pushX, m_pushY) : -1;
}

void ActionWheelComponent::tick(Microseconds now, TouchOutput& out)
{
    if (!m_tapEnd || now < *m_tapEnd) {
        return;
    }
    m_tapEnd.reset();
    if (m_tap) {
        out.up(*std::exchange(m_tap, std::nullopt), m_tapAt);
    }
}

void ActionWheelComponent::reset(TouchOutput& out)
{
    m_open = false;
    m_cell = -1;
    m_pushX = 0.0;
    m_pushY = 0.0;
    m_choice.reset();
    m_tapEnd.reset();
    if (m_tap) {
        out.cancel(*std::exchange(m_tap, std::nullopt), m_tapAt);
    }
}

const WheelSlot* ActionWheelComponent::slotAt(int cell) const noexcept
{
    if (cell < 0 || static_cast<std::size_t>(cell) >= m_config.cells.size()) {
        return nullptr;
    }
    return &m_config.cells[static_cast<std::size_t>(cell)];
}

int ActionWheelComponent::cellToward(double dx, double dy) const noexcept
{
    const int count = std::max<int>(static_cast<int>(m_config.cells.size()), 1);
    const double step = 360.0 / count;
    const double degrees = std::atan2(dx, -dy) * 180.0 / std::numbers::pi;
    const double turned = std::fmod(std::fmod(degrees, 360.0) + 360.0 + step / 2.0, 360.0);
    return std::clamp(static_cast<int>(turned / step), 0, count - 1);
}

void ActionWheelComponent::close(bool choose, Microseconds now, TouchOutput& out)
{
    const int chosen = m_cell;
    m_open = false;
    m_cell = -1;
    m_pushX = 0.0;
    m_pushY = 0.0;
    const WheelSlot* slot = choose ? slotAt(chosen) : nullptr;
    if (slot == nullptr) {
        return;
    }
    m_choice = chosen;
    if (slot->action == WheelAction::Tap && !m_tap) {
        m_tapAt = slot->at;
        m_tap = out.down(slot->at);
        if (m_tap) {
            m_tapEnd = now + m_config.tapHold;
        }
    }
}

void BehaviorComponent::signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out)
{
    if (slot != Slot::Press || signal == TriggerSignal::None) {
        return;
    }
    const auto first = std::ranges::find_if(
        m_config.shape.slotList, [](const BehaviorSlot& each) { return each.kind != BehaviorSlotKind::Axis2; });
    if (first == m_config.shape.slotList.end()) {
        return;
    }
    const auto lane = static_cast<std::uint32_t>(first - m_config.shape.slotList.begin());
    const bool trigger = first->kind == BehaviorSlotKind::Axis1;
    const auto press = [&](bool down) {
        const ModuleInput kind = trigger ? ModuleInput::Axis1 : (down ? ModuleInput::KeyDown : ModuleInput::KeyUp);
        input({.kind = kind, .slot = lane, .x = trigger && down ? 1.0 : 0.0, .y = 0.0, .at = now}, now, out);
    };
    if (signal != TriggerSignal::Deactivate) {
        press(true);
    }
    if (signal != TriggerSignal::Activate) {
        press(false);
    }
}

void BehaviorComponent::input(const ModuleEvent& event, Microseconds now, TouchOutput& out)
{
    if (!working() || event.slot >= m_config.shape.slotList.size()) {
        return;
    }
    note(event);
    ModuleOutput result;
    result.wakeAt = m_wake;
    const ModuleStatus status = m_instance->input(event, result);
    finish(status, result, now, out);
}

void BehaviorComponent::tick(Microseconds now, TouchOutput& out)
{
    if (!working()) {
        return;
    }
    if (m_wake && *m_wake <= now) {
        m_wake.reset();
        ModuleOutput result;
        const ModuleStatus status = m_instance->wake(now, result);
        finish(status, result, now, out);
        if (m_wake && *m_wake <= now) {
            m_wake = now + kModuleSoonest;
        }
        if (!working()) {
            return;
        }
    }
    if (m_frameAt && *m_frameAt <= now) {
        m_frameAt.reset();
        ModuleOutput result;
        result.wakeAt = m_wake;
        const ModuleStatus status = m_instance->frame(now, result);
        finish(status, result, now, out);
        if (!working()) {
            return;
        }
    }
    while (!m_held.empty() && m_held.front().at <= now) {
        const FingerCommand command = m_held.front();
        m_held.erase(m_held.begin());
        apply(command, out);
    }
    plan(now);
}

std::optional<Microseconds> BehaviorComponent::nextDeadline() const noexcept
{
    if (!working()) {
        return std::nullopt;
    }
    std::optional<Microseconds> earliest = m_wake;
    if (m_frameAt && (!earliest || *m_frameAt < *earliest)) {
        earliest = m_frameAt;
    }
    if (!m_held.empty() && (!earliest || m_held.front().at < *earliest)) {
        earliest = m_held.front().at;
    }
    return earliest;
}

void BehaviorComponent::reset(TouchOutput& out)
{
    lift(out);
    m_held.clear();
    m_wake.reset();
    m_frameAt.reset();
    m_keys = {};
    m_sticks = {};
    if (working()) {
        m_instance->reset();
    }
}

bool BehaviorComponent::touching() const noexcept
{
    return std::ranges::any_of(m_fingers, [](const std::optional<std::uint8_t>& finger) { return finger.has_value(); });
}

void BehaviorComponent::finish(ModuleStatus status, const ModuleOutput& result, Microseconds now, TouchOutput& out)
{
    if (status != ModuleStatus::Ok) {
        lift(out);
        m_held.clear();
        m_wake.reset();
        m_frameAt.reset();
        m_instance->reset();
        if (status == ModuleStatus::Failed) {
            m_stopped = true;
            m_trouble = ModuleTroubleKind::Stopped;
            return;
        }
        plan(now);
        return;
    }
    if (result.fresh) {
        lift(out);
        m_held.clear();
    }
    m_wake = result.wakeAt;
    for (const FingerCommand& command : result.commands) {
        if (command.at <= now) {
            apply(command, out);
        } else {
            m_held.insert(std::ranges::upper_bound(m_held, command.at, {}, &FingerCommand::at), command);
        }
    }
    plan(now);
}

void BehaviorComponent::apply(const FingerCommand& command, TouchOutput& out)
{
    if (command.finger >= static_cast<std::uint32_t>(m_config.shape.fingers) || command.finger >= kMostModuleFingers ||
        !std::isfinite(command.position.x) || !std::isfinite(command.position.y)) {
        return;
    }
    const Vec2 at{std::clamp(command.position.x, 0.0, 1.0), std::clamp(command.position.y, 0.0, 1.0)};
    std::optional<std::uint8_t>& finger = m_fingers[command.finger];
    switch (command.action) {
    case FingerAction::Down:
        if (finger) {
            out.move(*finger, at);
        } else {
            finger = out.down(at);
        }
        m_at[command.finger] = at;
        break;
    case FingerAction::Move:
        if (finger) {
            out.move(*finger, at);
            m_at[command.finger] = at;
        }
        break;
    case FingerAction::Up:
        if (finger) {
            out.up(*std::exchange(finger, std::nullopt), at);
        }
        break;
    }
}

void BehaviorComponent::lift(TouchOutput& out)
{
    for (std::size_t index = 0; index < m_fingers.size(); ++index) {
        if (m_fingers[index]) {
            out.cancel(*std::exchange(m_fingers[index], std::nullopt), m_at[index]);
        }
    }
}

void BehaviorComponent::plan(Microseconds now)
{
    if (!m_config.shape.frames || !working() || !active()) {
        m_frameAt.reset();
        return;
    }
    if (!m_frameAt) {
        m_frameAt = now + m_config.frame;
    }
}

void BehaviorComponent::note(const ModuleEvent& event) noexcept
{
    if (event.slot >= kMostBehaviorSlots) {
        return;
    }
    switch (event.kind) {
    case ModuleInput::KeyDown:
        m_keys[event.slot] = true;
        break;
    case ModuleInput::KeyUp:
        m_keys[event.slot] = false;
        break;
    case ModuleInput::Axis1:
        m_keys[event.slot] = event.x > 0.0;
        break;
    case ModuleInput::Axis2:
        m_sticks[event.slot] = {event.x, event.y};
        break;
    case ModuleInput::Mouse:
    case ModuleInput::Wheel:
        break;
    }
}

bool BehaviorComponent::active() const noexcept
{
    return std::ranges::any_of(m_keys, [](bool held) { return held; }) ||
           std::ranges::any_of(m_sticks, [](Vec2 stick) { return !isZero(stick); }) || touching();
}

}
