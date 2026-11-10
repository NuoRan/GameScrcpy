#include "UhidTouchSink.h"

#include <gs/session/Session.h>

#include <string>

namespace gs::app {

namespace {

bool lifts(const protocol::TouchPoint& point) noexcept
{
    return point.action == protocol::TouchAction::Up || point.action == protocol::TouchAction::Cancel;
}

}

bool SessionUhidWire::send(const protocol::UhidCreate& create)
{
    return m_session.send(create);
}

bool SessionUhidWire::send(const protocol::UhidInput& input)
{
    return m_session.send(input);
}

bool SessionUhidWire::send(const protocol::UhidDestroy& destroy)
{
    return m_session.send(destroy);
}

UhidTouchSink::UhidTouchSink(ControlSink& injection, UhidWire& wire) noexcept
    : m_injection(injection)
    , m_wire(wire)
{
}

bool UhidTouchSink::send(const protocol::TouchFrame& frame)
{
    const std::scoped_lock lock(m_mutex);
    return m_routed ? touch(frame) : inject(frame);
}

bool UhidTouchSink::send(const protocol::Key& key)
{
    return m_injection.send(key);
}

bool UhidTouchSink::send(const protocol::Scroll& scroll)
{
    return m_injection.send(scroll);
}

bool UhidTouchSink::send(const protocol::Text& text)
{
    return m_injection.send(text);
}

bool UhidTouchSink::padFeature(const protocol::UhidFeature& feature)
{
    return m_injection.padFeature(feature);
}

bool UhidTouchSink::padCreate(const protocol::UhidCreate& create)
{
    return m_injection.padCreate(create);
}

bool UhidTouchSink::padInput(const protocol::UhidInput& input)
{
    return m_injection.padInput(input);
}

bool UhidTouchSink::padDestroy(const protocol::UhidDestroy& destroy)
{
    return m_injection.padDestroy(destroy);
}

SinkCapabilities UhidTouchSink::capabilities() const
{
    const std::scoped_lock lock(m_mutex);
    if (m_welcomed && !m_offered) {
        return m_injection.capabilities();
    }
    return {.maxContacts = kUhidMaxContacts, .keys = true, .cancel = false};
}

void UhidTouchSink::detached()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_detached = true;
        if (m_created) {
            destroy();
        }
    }
    m_injection.detached();
}

void UhidTouchSink::welcomed(std::uint32_t phone)
{
    const std::scoped_lock lock(m_mutex);
    m_welcomed = true;
    m_offered = (phone & protocol::Welcome::kUhid) != 0;
}

void UhidTouchSink::phaseChanged(session::SessionPhase phase)
{
    const std::scoped_lock lock(m_mutex);
    switch (phase) {
    case session::SessionPhase::Streaming:
        if (m_offered && !m_created && !m_detached) {
            create();
        }
        break;
    case session::SessionPhase::Stopping:
        if (m_created) {
            destroy();
        }
        break;
    case session::SessionPhase::Reconnecting:
    case session::SessionPhase::Stopped:
    case session::SessionPhase::Failed:
        forget();
        break;
    default:
        break;
    }
}

void UhidTouchSink::setVideo(std::uint16_t session, int rotation)
{
    const std::scoped_lock lock(m_mutex);
    m_videoSession = session;
    m_rotation = rotation & 3;
}

bool UhidTouchSink::usesUhid() const
{
    const std::scoped_lock lock(m_mutex);
    return m_routed;
}

int UhidTouchSink::ignoredContacts() const
{
    const std::scoped_lock lock(m_mutex);
    return m_encoder.ignored();
}

bool UhidTouchSink::inject(const protocol::TouchFrame& frame)
{
    if (!m_injection.send(frame)) {
        return false;
    }
    for (const protocol::TouchPoint& point : frame.points) {
        if (point.pointerId > protocol::kMaxPointerId) {
            continue;
        }
        const std::uint32_t bit = 1U << point.pointerId;
        if (point.action == protocol::TouchAction::Down) {
            m_injected |= bit;
        } else if (lifts(point)) {
            m_injected &= ~bit;
        }
    }
    m_routed = m_created && m_injected == 0;
    return true;
}

bool UhidTouchSink::touch(const protocol::TouchFrame& frame)
{
    return m_encoder.encode(frame, frame.videoSession != m_videoSession, m_rotation, [this](const TouchReport& report) {
        protocol::UhidInput input;
        input.id = kUhidTouchId;
        input.report.assign(report.begin(), report.end());
        return m_wire.send(input);
    });
}

void UhidTouchSink::create()
{
    protocol::UhidCreate message;
    message.id = kUhidTouchId;
    message.vendorId = kUhidTouchVendor;
    message.productId = kUhidTouchProduct;
    message.name = std::string(kUhidTouchName);
    const std::span<const std::uint8_t> descriptor = uhidTouchDescriptor();
    message.reportDescriptor.assign(descriptor.begin(), descriptor.end());
    if (!m_wire.send(message)) {
        return;
    }
    m_created = true;
    m_encoder.reset();
    m_routed = m_injected == 0;
}

void UhidTouchSink::destroy()
{
    protocol::UhidDestroy message;
    message.id = kUhidTouchId;
    static_cast<void>(m_wire.send(message));
    m_created = false;
    m_routed = false;
    m_encoder.reset();
}

void UhidTouchSink::forget()
{
    m_created = false;
    m_routed = false;
    m_injected = 0;
    m_encoder.reset();
}

}
