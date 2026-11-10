#include "Esp32Session.h"

#include "Esp32Boards.h"
#include "ExtensionTouchSink.h"

#include <gs/exthost/Module.h>

#include <QMetaObject>
#include <QtLogging>

#include <atomic>
#include <utility>

namespace gs::app {

namespace {

constexpr std::uint32_t kMouseButtons = 0x07U;
constexpr int kScrollUnitsPerNotch = 256;

class QuietSink final : public ControlSink
{
public:
    [[nodiscard]] bool send(const protocol::TouchFrame&) override { return true; }
    [[nodiscard]] bool send(const protocol::Key&) override { return true; }
    [[nodiscard]] bool send(const protocol::Scroll&) override { return true; }
    [[nodiscard]] bool send(const protocol::Text&) override { return true; }
};

int notchesOf(int units) noexcept
{
    const int steps = units / kScrollUnitsPerNotch;
    return steps != 0 ? steps : units > 0 ? 1 : units < 0 ? -1 : 0;
}

}

class Esp32BoardSink final : public ControlSink
{
public:
    Esp32BoardSink(ExtensionTouch& touch, ExtensionTouchSink* board) noexcept
        : m_touch(touch)
        , m_board(board)
    {
    }

    [[nodiscard]] bool send(const protocol::TouchFrame& frame) override { return m_touch.send(frame); }
    [[nodiscard]] bool send(const protocol::Key& key) override { return m_touch.send(key); }
    [[nodiscard]] bool send(const protocol::Scroll& scroll) override
    {
        const int wheel = notchesOf(scroll.vScroll);
        return wheel == 0 || mouse(0, 0, wheel);
    }
    [[nodiscard]] bool send(const protocol::Text& text) override { return m_touch.send(text); }
    [[nodiscard]] SinkCapabilities capabilities() const override { return m_touch.capabilities(); }
    void detached() override { m_touch.detached(); }
    [[nodiscard]] bool padExternal(const input::PadPassState& state) override { return m_touch.padExternal(state); }

    [[nodiscard]] bool mouse(int dx, int dy, int wheel)
    {
        return m_board != nullptr && m_board->mouse(exthost::MouseEvent{.buttons = m_buttons.load(std::memory_order_acquire),
                                                                        .dx = dx,
                                                                        .dy = dy,
                                                                        .wheel = wheel});
    }

    void setButtons(std::uint32_t buttons) noexcept { m_buttons.store(buttons, std::memory_order_release); }

private:
    ExtensionTouch& m_touch;
    ExtensionTouchSink* const m_board;
    std::atomic<std::uint32_t> m_buttons{0};
};

Esp32Session::Esp32Session(QString name, QObject* parent)
    : QObject(parent)
    , m_name(std::move(name))
    , m_quiet(std::make_unique<QuietSink>())
{
}

Esp32Session::~Esp32Session()
{
    release();
}

bool Esp32Session::active() const noexcept
{
    return m_phase != session::SessionPhase::Idle && m_phase != session::SessionPhase::Stopped &&
           m_phase != session::SessionPhase::Failed;
}

void Esp32Session::start(TouchMethodSource* source, const std::string& device, int rotation, QSize canvas)
{
    if (active()) {
        return;
    }
    release();
    enter(session::SessionPhase::Connecting);
    const std::uint32_t generation = ++m_generation;
    std::unique_ptr<ExtensionTouch> touch;
    if (source != nullptr) {
        touch = source->openTouch(kEsp32Method, device, *m_quiet, [this, generation] {
            QMetaObject::invokeMethod(this, [this, generation] { fellBack(generation); }, Qt::QueuedConnection);
        });
    }
    if (touch == nullptr || touch->fellBack()) {
        qWarning("ESP32 touch box for %s: the touch method did not open", qUtf8Printable(m_name));
        session::SessionFailure failure;
        failure.kind = session::FailureKind::ConnectFailed;
        failure.phase = session::SessionPhase::Connecting;
        failure.detail = "the ESP32 touch box did not open";
        enter(session::SessionPhase::Failed, failure);
        return;
    }
    m_touch = std::move(touch);
    m_sink = std::make_unique<Esp32BoardSink>(*m_touch, dynamic_cast<ExtensionTouchSink*>(m_touch.get()));
    m_channel->attach(*m_sink);
    setOrientation(rotation, canvas);
    qInfo("ESP32 touch box for %s: touches, keys and the mouse go through the board, without a picture",
          qUtf8Printable(m_name));
    enter(session::SessionPhase::Streaming);
}

void Esp32Session::stop()
{
    if (!active()) {
        return;
    }
    enter(session::SessionPhase::Stopping);
    release();
    enter(session::SessionPhase::Stopped);
}

void Esp32Session::setOrientation(int rotation, QSize canvas)
{
    m_canvas = canvas;
    m_rotation = rotation & 3;
    m_videoSession = static_cast<std::uint16_t>(m_videoSession % 0xFFFFU + 1U);
    if (m_touch != nullptr) {
        m_touch->setVideo(m_videoSession, m_rotation);
    }
    m_channel->setVideo(m_videoSession, static_cast<std::uint16_t>(canvas.width()),
                        static_cast<std::uint16_t>(canvas.height()));
}

void Esp32Session::pressKey(std::uint32_t keycode)
{
    protocol::Key key;
    key.keycode = keycode;
    key.action = protocol::KeyAction::Down;
    const bool down = m_channel->send(key);
    key.action = protocol::KeyAction::Up;
    const bool up = m_channel->send(key);
    if (!down || !up) {
        qInfo("ESP32 touch box for %s: key %u did not reach the board", qUtf8Printable(m_name), keycode);
    }
}

void Esp32Session::moveMouse(int dx, int dy)
{
    if (m_sink != nullptr) {
        static_cast<void>(m_sink->mouse(dx, dy, 0));
    }
}

void Esp32Session::setMouseButtons(int buttons)
{
    if (m_sink == nullptr) {
        return;
    }
    m_sink->setButtons(static_cast<std::uint32_t>(buttons) & kMouseButtons);
    static_cast<void>(m_sink->mouse(0, 0, 0));
}

void Esp32Session::fellBack(std::uint32_t generation)
{
    if (generation != m_generation || !active() || m_touch == nullptr || !m_touch->fellBack()) {
        return;
    }
    qWarning("ESP32 touch box for %s: the board stopped answering", qUtf8Printable(m_name));
    const session::SessionPhase during = m_phase;
    release();
    session::SessionFailure failure;
    failure.kind = session::FailureKind::ConnectionLost;
    failure.phase = during;
    failure.detail = "the ESP32 touch box stopped answering";
    enter(session::SessionPhase::Failed, failure);
}

void Esp32Session::enter(session::SessionPhase phase, std::optional<session::SessionFailure> failure)
{
    const bool streams = phase == session::SessionPhase::Streaming;
    if (streams && m_phase != session::SessionPhase::Streaming) {
        ++m_padEpoch;
    }
    m_phase = phase;
    const bool pad = streams && m_touch != nullptr && m_touch->capabilities().gamepad;
    if (m_channel->setPadLink({.route = pad ? PadRoute::External : PadRoute::None, .epoch = m_padEpoch})) {
        emit padLinkChanged();
    }
    emit stateChanged({.phase = phase, .failure = std::move(failure)});
}

void Esp32Session::release()
{
    m_channel->detach();
    m_sink.reset();
    m_touch.reset();
}

}
