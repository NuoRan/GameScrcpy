#include "ExtensionTouchSink.h"

#include <algorithm>
#include <utility>

namespace gs::app {

namespace {

constexpr double kPictureScale = 65535.0;

bool lifts(const protocol::TouchPoint& point) noexcept
{
    return point.action == protocol::TouchAction::Up || point.action == protocol::TouchAction::Cancel;
}

}

exthost::PadEvent padEventOf(const input::PadPassState& state) noexcept
{
    return exthost::PadEvent{.buttons = state.buttons,
                             .hat = state.hat,
                             .leftX = state.leftX,
                             .leftY = state.leftY,
                             .rightX = state.rightX,
                             .rightY = state.rightY,
                             .leftTrigger = state.leftTrigger,
                             .rightTrigger = state.rightTrigger};
}

ModuleTouchPort::ModuleTouchPort(std::shared_ptr<exthost::Module> module, std::size_t method, std::string_view device,
                                 std::string_view options)
    : m_module(std::move(module))
{
    if (m_module == nullptr || method >= m_module->info().touchMethods.size()) {
        return;
    }
    const exthost::TouchMethodInfo& info = m_module->info().touchMethods[method];
    m_capabilities =
        SinkCapabilities{.maxContacts = std::clamp(info.maxContacts, 1, static_cast<int>(exthost::kMostContacts)),
                         .keys = info.keys,
                         .cancel = info.canCancel,
                         .gamepad = info.gamepad};
    m_typing = info.text;
    m_mouse = info.mouse;
    m_session = m_module->openTouch(method, device, options);
}

ModuleTouchPort::~ModuleTouchPort()
{
    close();
}

bool ModuleTouchPort::frame(const exthost::TouchFrame& frame)
{
    return working() && m_module->touch(m_session, frame);
}

bool ModuleTouchPort::key(const protocol::Key& key)
{
    return working() && m_capabilities.keys &&
           m_module->key(m_session, exthost::KeyEvent{.keycode = key.keycode,
                                                      .down = key.action == protocol::KeyAction::Down,
                                                      .repeat = key.repeat,
                                                      .meta = key.metaState});
}

bool ModuleTouchPort::text(std::string_view utf8)
{
    return working() && m_typing && m_module->text(m_session, utf8);
}

bool ModuleTouchPort::mouse(const exthost::MouseEvent& mouse)
{
    return working() && m_mouse && m_module->mouse(m_session, mouse);
}

bool ModuleTouchPort::gamepad(const exthost::PadEvent& pad)
{
    return working() && m_capabilities.gamepad && m_module->gamepad(m_session, pad);
}

bool ModuleTouchPort::working() const
{
    return m_module != nullptr && m_session != 0 && !m_lost.load(std::memory_order_acquire) &&
           !m_closed.load(std::memory_order_acquire) && m_module->working();
}

void ModuleTouchPort::close()
{
    if (m_module != nullptr && m_session != 0 && !m_closed.exchange(true, std::memory_order_acq_rel)) {
        m_module->closeTouch(m_session);
    }
}

ExtensionTouchSink::ExtensionTouchSink(ControlSink& injection, std::shared_ptr<TouchPort> port,
                                       std::function<void()> fellBack)
    : m_injection(injection)
    , m_port(std::move(port))
    , m_fellBack(std::move(fellBack))
    , m_capabilities(m_port != nullptr ? m_port->capabilities() : SinkCapabilities{})
{
    m_capabilities.maxContacts = std::clamp(m_capabilities.maxContacts, 1, static_cast<int>(exthost::kMostContacts));
    m_fallen = m_port == nullptr || !m_port->working();
}

ExtensionTouchSink::~ExtensionTouchSink()
{
    const std::scoped_lock lock(m_mutex);
    if (!m_closed) {
        if (!m_fallen) {
            liftAll();
        }
        if (m_port != nullptr) {
            m_port->close();
        }
        m_closed = true;
    }
}

exthost::Contact ExtensionTouchSink::contactOf(std::size_t slot, exthost::TouchAction action, std::uint16_t x,
                                               std::uint16_t y, double pressure) const
{
    const double u = x / kPictureScale;
    const double v = y / kPictureScale;
    double naturalX = u;
    double naturalY = v;
    switch (m_rotation & 3) {
    case 1:
        naturalX = 1.0 - v;
        naturalY = u;
        break;
    case 2:
        naturalX = 1.0 - u;
        naturalY = 1.0 - v;
        break;
    case 3:
        naturalX = v;
        naturalY = 1.0 - u;
        break;
    default:
        break;
    }
    return exthost::Contact{.id = static_cast<std::uint32_t>(slot),
                            .action = action,
                            .x = u,
                            .y = v,
                            .naturalX = naturalX,
                            .naturalY = naturalY,
                            .pressure = pressure};
}

void ExtensionTouchSink::fallBack()
{
    m_fallen = true;
    m_slots = {};
    m_injected = 0;
    if (m_port != nullptr) {
        m_port->close();
    }
}

void ExtensionTouchSink::liftAll()
{
    exthost::TouchFrame lifted;
    lifted.rotation = static_cast<std::uint32_t>(m_rotation & 3);
    const exthost::TouchAction action = m_capabilities.cancel ? exthost::TouchAction::Cancel : exthost::TouchAction::Up;
    for (std::size_t slot = 0; slot < m_slots.size(); ++slot) {
        if (m_slots[slot].held) {
            lifted.contacts.push_back(contactOf(slot, action, m_slots[slot].x, m_slots[slot].y, 0.0));
            m_slots[slot].held = false;
        }
    }
    if (!lifted.contacts.empty() && m_port != nullptr) {
        static_cast<void>(m_port->frame(lifted));
    }
}

bool ExtensionTouchSink::injectAfterFallback(const protocol::TouchFrame& frame)
{
    protocol::TouchFrame kept;
    kept.videoSession = frame.videoSession;
    for (const protocol::TouchPoint& point : frame.points) {
        if (point.pointerId > protocol::kMaxPointerId) {
            continue;
        }
        const std::uint32_t bit = 1U << point.pointerId;
        if (point.action == protocol::TouchAction::Down) {
            m_injected |= bit;
            kept.points.push_back(point);
        } else if ((m_injected & bit) != 0) {
            if (lifts(point)) {
                m_injected &= ~bit;
            }
            kept.points.push_back(point);
        }
    }
    return kept.points.empty() || m_injection.send(kept);
}

bool ExtensionTouchSink::send(const protocol::TouchFrame& frame)
{
    bool fell = false;
    bool sent = true;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return false;
        }
        if (!m_fallen && !m_port->working()) {
            fallBack();
            fell = true;
        }
        if (m_fallen) {
            sent = injectAfterFallback(frame);
        } else {
            exthost::TouchFrame out;
            out.rotation = static_cast<std::uint32_t>(m_rotation & 3);
            const bool stale = frame.videoSession != m_videoSession;
            std::uint32_t freed = 0;
            for (const protocol::TouchPoint& point : frame.points) {
                const auto held = std::ranges::find_if(
                    m_slots, [&point](const Slot& slot) { return slot.held && slot.pointerId == point.pointerId; });
                const auto slot = static_cast<std::size_t>(held - m_slots.begin());
                const double pressure = point.pressure / kPictureScale;
                if (lifts(point)) {
                    if (held == m_slots.end()) {
                        continue;
                    }
                    const bool cancel = point.action == protocol::TouchAction::Cancel;
                    if (!stale && !cancel) {
                        held->x = point.x;
                        held->y = point.y;
                    }
                    out.contacts.push_back(contactOf(slot,
                                                     cancel && m_capabilities.cancel ? exthost::TouchAction::Cancel
                                                                                     : exthost::TouchAction::Up,
                                                     held->x, held->y, 0.0));
                    held->held = false;
                    freed |= 1U << slot;
                    continue;
                }
                if (stale) {
                    continue;
                }
                if (held != m_slots.end()) {
                    held->x = point.x;
                    held->y = point.y;
                    out.contacts.push_back(contactOf(slot, exthost::TouchAction::Move, point.x, point.y, pressure));
                    continue;
                }
                if (point.action != protocol::TouchAction::Down) {
                    continue;
                }
                const auto limit = static_cast<std::size_t>(m_capabilities.maxContacts);
                std::size_t chosen = limit;
                for (std::size_t index = 0; index < limit; ++index) {
                    if (!m_slots[index].held && (chosen == limit || ((freed >> chosen) & 1U) != 0)) {
                        chosen = index;
                        if (((freed >> index) & 1U) == 0) {
                            break;
                        }
                    }
                }
                if (chosen == limit) {
                    ++m_ignored;
                    continue;
                }
                m_slots[chosen] = Slot{.held = true, .pointerId = point.pointerId, .x = point.x, .y = point.y};
                out.contacts.push_back(contactOf(chosen, exthost::TouchAction::Down, point.x, point.y, pressure));
            }
            if (!out.contacts.empty() && !m_port->frame(out)) {
                fallBack();
                fell = true;
                sent = injectAfterFallback(frame);
            }
        }
    }
    if (fell && m_fellBack) {
        m_fellBack();
    }
    return sent;
}

bool ExtensionTouchSink::send(const protocol::Key& key)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_fallen && !m_closed && m_capabilities.keys && m_port->key(key)) {
            return true;
        }
    }
    return m_injection.send(key);
}

bool ExtensionTouchSink::send(const protocol::Scroll& scroll)
{
    return m_injection.send(scroll);
}

bool ExtensionTouchSink::send(const protocol::Text& text)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_fallen && !m_closed && m_port->text(text.text)) {
            return true;
        }
    }
    return m_injection.send(text);
}

bool ExtensionTouchSink::padExternal(const input::PadPassState& state)
{
    const std::scoped_lock lock(m_mutex);
    return !m_fallen && !m_closed && m_capabilities.gamepad && m_port->gamepad(padEventOf(state));
}

bool ExtensionTouchSink::mouse(const exthost::MouseEvent& mouse)
{
    const std::scoped_lock lock(m_mutex);
    return !m_fallen && !m_closed && m_port->mouse(mouse);
}

SinkCapabilities ExtensionTouchSink::capabilities() const
{
    const std::scoped_lock lock(m_mutex);
    return m_fallen ? m_injection.capabilities() : m_capabilities;
}

void ExtensionTouchSink::detached()
{
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_closed) {
            if (!m_fallen) {
                liftAll();
            }
            if (m_port != nullptr) {
                m_port->close();
            }
            m_closed = true;
        }
    }
    m_injection.detached();
}

void ExtensionTouchSink::setVideo(std::uint16_t session, int rotation)
{
    const std::scoped_lock lock(m_mutex);
    m_videoSession = session;
    m_rotation = rotation & 3;
}

bool ExtensionTouchSink::fellBack() const
{
    const std::scoped_lock lock(m_mutex);
    return m_fallen;
}

int ExtensionTouchSink::ignoredContacts() const
{
    const std::scoped_lock lock(m_mutex);
    return m_ignored;
}

}
