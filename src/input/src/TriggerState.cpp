#include "gs/input/TriggerState.h"

namespace gs::input {

TriggerState::TriggerState(const TriggerConfig& config, std::optional<Microseconds> tapDecidedOnRelease) noexcept
    : m_trigger(config.trigger)
    , m_longPress(Microseconds{config.longPressMs * 1000LL})
    , m_doubleTapWindow(Microseconds{config.doubleTapMs * 1000LL})
    , m_turboPeriod(Microseconds{1'000'000LL / (config.rateHz > 0 ? config.rateHz : 1)})
    , m_tapDecidedOnRelease(config.trigger == Trigger::Tap ? tapDecidedOnRelease : std::nullopt)
{
}

TriggerSignal TriggerState::press(Microseconds now) noexcept
{
    if (m_pressed) {
        return TriggerSignal::None;
    }
    m_pressed = true;
    m_pressedAt = now;
    switch (m_trigger) {
    case Trigger::Hold:
        m_active = true;
        return TriggerSignal::Activate;
    case Trigger::Tap:
        return m_tapDecidedOnRelease ? TriggerSignal::None : TriggerSignal::Pulse;
    case Trigger::LongPress:
        m_deadline = now + m_longPress;
        return TriggerSignal::None;
    case Trigger::DoubleTap:
        if (m_firstTapSeen && m_deadline && now <= *m_deadline) {
            m_firstTapSeen = false;
            m_deadline.reset();
            m_active = true;
            return TriggerSignal::Activate;
        }
        m_firstTapSeen = true;
        m_deadline = now + m_doubleTapWindow;
        return TriggerSignal::None;
    case Trigger::Toggle:
        m_active = !m_active;
        return m_active ? TriggerSignal::Activate : TriggerSignal::Deactivate;
    case Trigger::Turbo:
        m_deadline = now + m_turboPeriod;
        return TriggerSignal::Pulse;
    case Trigger::Release:
        return TriggerSignal::None;
    }
    return TriggerSignal::None;
}

TriggerSignal TriggerState::release(Microseconds now) noexcept
{
    if (!m_pressed) {
        return TriggerSignal::None;
    }
    m_pressed = false;
    switch (m_trigger) {
    case Trigger::Hold:
    case Trigger::LongPress:
        m_deadline.reset();
        if (m_active) {
            m_active = false;
            return TriggerSignal::Deactivate;
        }
        return TriggerSignal::None;
    case Trigger::DoubleTap:
        if (m_active) {
            m_active = false;
            return TriggerSignal::Deactivate;
        }
        return TriggerSignal::None;
    case Trigger::Tap:
        return m_tapDecidedOnRelease && now - m_pressedAt < *m_tapDecidedOnRelease ? TriggerSignal::Pulse
                                                                                    : TriggerSignal::None;
    case Trigger::Turbo:
        m_deadline.reset();
        return TriggerSignal::None;
    case Trigger::Release:
        return TriggerSignal::Pulse;
    case Trigger::Toggle:
        return TriggerSignal::None;
    }
    return TriggerSignal::None;
}

TriggerSignal TriggerState::tick(Microseconds now) noexcept
{
    if (!m_deadline || now < *m_deadline) {
        return TriggerSignal::None;
    }
    switch (m_trigger) {
    case Trigger::LongPress:
        m_deadline.reset();
        m_active = true;
        return TriggerSignal::Activate;
    case Trigger::DoubleTap:
        m_deadline.reset();
        m_firstTapSeen = false;
        return TriggerSignal::None;
    case Trigger::Turbo: {
        const Microseconds next = *m_deadline + m_turboPeriod;
        m_deadline = next > now ? next : now + m_turboPeriod;
        return TriggerSignal::Pulse;
    }
    default:
        m_deadline.reset();
        return TriggerSignal::None;
    }
}

void TriggerState::reset() noexcept
{
    m_pressed = false;
    m_active = false;
    m_firstTapSeen = false;
    m_deadline.reset();
}

void TriggerState::retune(const TriggerConfig& config) noexcept
{
    m_longPress = Microseconds{config.longPressMs * 1000LL};
    m_doubleTapWindow = Microseconds{config.doubleTapMs * 1000LL};
    m_turboPeriod = Microseconds{1'000'000LL / (config.rateHz > 0 ? config.rateHz : 1)};
}

}
