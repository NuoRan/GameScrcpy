#include "AoaSession.h"

#include <QMetaObject>
#include <QtLogging>

#include <utility>

namespace gs::app {

namespace {

const char* failureName(platform::UsbFailure failure) noexcept
{
    switch (failure) {
    case platform::UsbFailure::None:
        return "no problem";
    case platform::UsbFailure::NotFound:
        return "not found on USB";
    case platform::UsbFailure::NoDriver:
        return "no WinUSB driver";
    case platform::UsbFailure::Busy:
        return "in use by another program";
    case platform::UsbFailure::Gone:
        return "unplugged";
    case platform::UsbFailure::Refused:
        return "refused";
    }
    return "?";
}

}

AoaSession::AoaSession(QString name, QObject* parent)
    : QObject(parent)
    , m_name(std::move(name))
{
}

AoaSession::~AoaSession()
{
    release();
}

bool AoaSession::active() const noexcept
{
    return m_phase != session::SessionPhase::Idle && m_phase != session::SessionPhase::Stopped &&
           m_phase != session::SessionPhase::Failed;
}

void AoaSession::start(const platform::UsbPhone& phone, int rotation, QSize canvas)
{
    if (active()) {
        return;
    }
    release();
    m_failure = platform::UsbFailure::None;
    m_portState = AoaState::Starting;
    enter(session::SessionPhase::Connecting);
    std::unique_ptr<AoaLink> link = openAoaLink(phone, m_failure);
    if (link == nullptr) {
        qWarning("AOA on %s: the phone cannot be opened over USB, %s", qUtf8Printable(m_name), failureName(m_failure));
        session::SessionFailure failure;
        failure.kind = m_failure == platform::UsbFailure::NotFound ? session::FailureKind::DeviceNotFound
                                                                   : session::FailureKind::ConnectFailed;
        failure.phase = session::SessionPhase::Connecting;
        failure.detail = failureName(m_failure);
        enter(session::SessionPhase::Failed, failure);
        return;
    }
    m_port = std::make_unique<AoaPort>(std::move(link), [this](AoaState state) {
        QMetaObject::invokeMethod(this, [this, state] { portChanged(state); }, Qt::QueuedConnection);
    });
    m_sink = std::make_unique<AoaTouchSink>(nullptr, *m_port, AoaDevices{.mouse = true, .keyboard = true});
    m_channel->attach(*m_sink);
    setOrientation(rotation, canvas);
    qInfo("AOA on %s: a touch screen, a mouse and a keyboard over the USB cable, without a picture",
          qUtf8Printable(m_name));
}

void AoaSession::stop()
{
    if (!active()) {
        return;
    }
    enter(session::SessionPhase::Stopping);
    release();
    enter(session::SessionPhase::Stopped);
}

void AoaSession::setOrientation(int rotation, QSize canvas)
{
    m_canvas = canvas;
    m_videoSession = static_cast<std::uint16_t>(m_videoSession % 0xFFFFU + 1U);
    if (m_sink != nullptr) {
        m_sink->setVideo(m_videoSession, rotation);
    }
    m_channel->setVideo(m_videoSession, static_cast<std::uint16_t>(canvas.width()),
                        static_cast<std::uint16_t>(canvas.height()));
}

void AoaSession::pressKey(std::uint32_t keycode)
{
    if (m_sink != nullptr && !m_sink->pressKey(keycode)) {
        qInfo("AOA on %s: key %u has no keyboard key to send", qUtf8Printable(m_name), keycode);
    }
}

void AoaSession::moveMouse(int dx, int dy)
{
    if (m_sink != nullptr) {
        static_cast<void>(m_sink->moveMouse(dx, dy));
    }
}

void AoaSession::setMouseButtons(int buttons)
{
    if (m_sink != nullptr) {
        static_cast<void>(m_sink->setMouseButtons(static_cast<std::uint8_t>(buttons)));
    }
}

void AoaSession::portChanged(AoaState state)
{
    m_portState = state;
    switch (state) {
    case AoaState::Ready:
        if (m_phase == session::SessionPhase::Connecting) {
            enter(session::SessionPhase::Streaming);
        }
        return;
    case AoaState::Unsupported:
    case AoaState::Failed:
    case AoaState::Gone:
        break;
    case AoaState::Starting:
        return;
    }
    if (!active()) {
        return;
    }
    const bool gone = state == AoaState::Gone;
    m_failure = gone ? platform::UsbFailure::Gone : platform::UsbFailure::Refused;
    qWarning("AOA on %s: %s", qUtf8Printable(m_name),
             gone                                ? "the phone was unplugged"
             : state == AoaState::Unsupported    ? "the phone does not take HID devices over AOA"
                                                 : "the phone refused the HID devices");
    const session::SessionPhase during = m_phase;
    release();
    session::SessionFailure failure;
    failure.kind = gone ? session::FailureKind::ConnectionLost : session::FailureKind::HandshakeRejected;
    failure.phase = during;
    failure.detail = gone ? "unplugged" : "refused";
    enter(session::SessionPhase::Failed, failure);
}

void AoaSession::enter(session::SessionPhase phase, std::optional<session::SessionFailure> failure)
{
    const bool streams = phase == session::SessionPhase::Streaming;
    if (streams && m_phase != session::SessionPhase::Streaming) {
        ++m_padEpoch;
    }
    m_phase = phase;
    if (m_channel->setPadLink({.route = streams ? PadRoute::Aoa : PadRoute::None, .epoch = m_padEpoch})) {
        emit padLinkChanged();
    }
    emit stateChanged({.phase = phase, .failure = std::move(failure)});
}

void AoaSession::release()
{
    m_channel->detach();
    m_sink.reset();
    m_port.reset();
}

}
