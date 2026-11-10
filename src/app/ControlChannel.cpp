#include "ControlChannel.h"

#include <gs/session/Session.h>

#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

namespace gs::app {

namespace {

std::int64_t steadyMicros() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class TouchTrace
{
public:
    static TouchTrace& instance()
    {
        static TouchTrace trace;
        return trace;
    }

    void frame(const protocol::TouchFrame& frame, bool sent)
    {
        if (!m_out.is_open()) {
            return;
        }
        const std::int64_t now = steadyMicros();
        for (const protocol::TouchPoint& point : frame.points) {
            m_out << now << ' ' << (sent ? 's' : 'x') << ' ' << static_cast<int>(point.pointerId) << ' '
                  << static_cast<int>(point.action) << ' ' << point.x << ' ' << point.y << '\n';
        }
    }

private:
    TouchTrace()
    {
        const QString path = qEnvironmentVariable("GS_TOUCH_TRACE");
        if (!path.isEmpty()) {
            m_out.open(std::filesystem::path(path.toStdWString()), std::ios::out | std::ios::app);
        }
    }

    std::ofstream m_out;
};

}

bool SessionControlSink::send(const protocol::TouchFrame& frame)
{
    return m_session.send(frame);
}

bool SessionControlSink::send(const protocol::Key& key)
{
    return m_session.send(key);
}

bool SessionControlSink::send(const protocol::Scroll& scroll)
{
    return m_session.send(scroll);
}

bool SessionControlSink::send(const protocol::Text& text)
{
    return m_session.send(text);
}

bool SessionControlSink::padFeature(const protocol::UhidFeature& feature)
{
    return m_session.send(feature);
}

bool SessionControlSink::padCreate(const protocol::UhidCreate& create)
{
    return m_session.send(create);
}

bool SessionControlSink::padInput(const protocol::UhidInput& input)
{
    return m_session.send(input);
}

bool SessionControlSink::padDestroy(const protocol::UhidDestroy& destroy)
{
    return m_session.send(destroy);
}

void ControlChannel::attach(ControlSink& sink)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_sink != nullptr && m_sink != &sink) {
            m_sink->detached();
        }
        m_sink = &sink;
        m_screen.reset();
    }
    changed();
}

void ControlChannel::detach()
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_sink != nullptr) {
            m_sink->detached();
        }
        m_sink = nullptr;
        m_screen.reset();
    }
    changed();
}

void ControlChannel::setVideo(std::uint16_t videoSession, std::uint16_t width, std::uint16_t height)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_sink == nullptr) {
            return;
        }
        InputScreen next;
        next.videoSession = videoSession;
        next.geometry.width = width;
        next.geometry.height = height;
        next.refreshHz = m_refreshHz;
        m_screen = next;
    }
    changed();
}

void ControlChannel::setRefreshHz(double hz)
{
    if (!(hz > 0.0)) {
        return;
    }
    {
        const std::scoped_lock lock(m_mutex);
        if (hz == m_refreshHz) {
            return;
        }
        m_refreshHz = hz;
        if (m_screen) {
            m_screen->refreshHz = hz;
        }
    }
    m_pacing.fetch_add(1, std::memory_order_acq_rel);
}

double ControlChannel::refreshHz() const
{
    const std::scoped_lock lock(m_mutex);
    return m_refreshHz;
}

std::optional<InputScreen> ControlChannel::screen() const
{
    const std::scoped_lock lock(m_mutex);
    return m_screen;
}

SinkCapabilities ControlChannel::capabilities() const
{
    const std::scoped_lock lock(m_mutex);
    return m_sink != nullptr ? m_sink->capabilities() : SinkCapabilities{};
}

void ControlChannel::setTouchObserver(TouchObserver observer)
{
    const std::scoped_lock lock(m_mutex);
    m_touchObserver = std::move(observer);
}

void ControlChannel::setOperationObserver(std::shared_ptr<OperationObserver> observer)
{
    const std::scoped_lock lock(m_mutex);
    m_operations = std::move(observer);
}

bool ControlChannel::send(const protocol::TouchFrame& frame)
{
    const std::scoped_lock lock(m_mutex);
    const bool sent = m_sink != nullptr && m_screen.has_value() && m_sink->send(frame);
    TouchTrace::instance().frame(frame, sent);
    if (!sent) {
        return false;
    }
    if (m_touchObserver) {
        m_touchObserver(frame, steadyMicros());
    }
    if (m_operations != nullptr) {
        m_operations->touched(frame);
    }
    return true;
}

bool ControlChannel::send(const protocol::Key& key)
{
    const std::scoped_lock lock(m_mutex);
    const bool sent = m_sink != nullptr && m_sink->send(key);
    if (sent && m_operations != nullptr) {
        m_operations->keyed(key);
    }
    return sent;
}

bool ControlChannel::scroll(input::Vec2 position, std::int16_t horizontal, std::int16_t vertical)
{
    const std::scoped_lock lock(m_mutex);
    if (m_sink == nullptr || !m_screen) {
        return false;
    }
    protocol::Scroll message;
    message.x = input::fixedCoordinate(position.x);
    message.y = input::fixedCoordinate(position.y);
    message.videoSession = m_screen->videoSession;
    message.hScroll = horizontal;
    message.vScroll = vertical;
    const bool sent = m_sink->send(message);
    if (sent && m_operations != nullptr) {
        m_operations->scrolled(message);
    }
    return sent;
}

bool ControlChannel::type(std::string_view utf8)
{
    constexpr std::size_t kMostTextBytes = 4096;
    const std::scoped_lock lock(m_mutex);
    if (m_sink == nullptr) {
        return false;
    }
    const auto characters = static_cast<std::size_t>(
        std::ranges::count_if(utf8, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
    bool sent = true;
    while (!utf8.empty()) {
        std::size_t size = std::min(utf8.size(), kMostTextBytes);
        while (size > 0 && size < utf8.size() && (static_cast<unsigned char>(utf8[size]) & 0xC0) == 0x80) {
            --size;
        }
        if (size == 0) {
            size = std::min(utf8.size(), kMostTextBytes);
        }
        protocol::Text message;
        message.text.assign(utf8.substr(0, size));
        sent = m_sink->send(message) && sent;
        utf8.remove_prefix(size);
    }
    if (sent && characters > 0 && m_operations != nullptr) {
        m_operations->typed(characters);
    }
    return sent;
}

bool ControlChannel::setPadLink(PadLink link)
{
    const std::scoped_lock lock(m_mutex);
    if (link == m_padLink) {
        return false;
    }
    m_padLink = link;
    return true;
}

PadLink ControlChannel::padLink() const
{
    const std::scoped_lock lock(m_mutex);
    return m_padLink;
}

bool ControlChannel::padFeature(const protocol::UhidFeature& feature)
{
    const std::scoped_lock lock(m_mutex);
    return m_sink != nullptr && m_sink->padFeature(feature);
}

bool ControlChannel::padCreate(const protocol::UhidCreate& create)
{
    const std::scoped_lock lock(m_mutex);
    return m_sink != nullptr && m_sink->padCreate(create);
}

bool ControlChannel::padInput(const protocol::UhidInput& input)
{
    const std::scoped_lock lock(m_mutex);
    return m_sink != nullptr && m_sink->padInput(input);
}

bool ControlChannel::padDestroy(const protocol::UhidDestroy& destroy)
{
    const std::scoped_lock lock(m_mutex);
    return m_sink != nullptr && m_sink->padDestroy(destroy);
}

bool ControlChannel::externalPad() const
{
    const std::scoped_lock lock(m_mutex);
    return m_sink != nullptr && m_sink->capabilities().gamepad;
}

bool ControlChannel::padExternal(const input::PadPassState& state)
{
    const std::scoped_lock lock(m_mutex);
    return m_sink != nullptr && m_sink->padExternal(state);
}

}
