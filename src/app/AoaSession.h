#pragma once

#include "AoaTouchSink.h"
#include "ControlChannel.h"

#include <gs/platform/Usb.h>
#include <gs/render/FrameSlot.h>
#include <gs/session/SessionState.h>

#include <QObject>
#include <QSize>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace gs::app {

class AoaSession final : public QObject
{
    Q_OBJECT

public:
    explicit AoaSession(QString name, QObject* parent = nullptr);
    ~AoaSession() override;

    void start(const platform::UsbPhone& phone, int rotation, QSize canvas);
    void stop();
    void setOrientation(int rotation, QSize canvas);
    void pressKey(std::uint32_t keycode);
    void moveMouse(int dx, int dy);
    void setMouseButtons(int buttons);

    [[nodiscard]] session::SessionPhase phase() const noexcept { return m_phase; }
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] platform::UsbFailure failure() const noexcept { return m_failure; }
    [[nodiscard]] AoaState portState() const noexcept { return m_portState; }
    [[nodiscard]] QSize canvas() const noexcept { return m_canvas; }
    [[nodiscard]] const std::shared_ptr<ControlChannel>& channel() const noexcept { return m_channel; }
    [[nodiscard]] const std::shared_ptr<render::FrameSlot>& frames() const noexcept { return m_frames; }

signals:
    void stateChanged(const gs::session::SessionState& state);
    void padLinkChanged();

private:
    void portChanged(AoaState state);
    void enter(session::SessionPhase phase, std::optional<session::SessionFailure> failure = std::nullopt);
    void release();
    std::uint32_t m_padEpoch = 0;

    const QString m_name;
    const std::shared_ptr<ControlChannel> m_channel = std::make_shared<ControlChannel>();
    const std::shared_ptr<render::FrameSlot> m_frames = std::make_shared<render::FrameSlot>();
    std::unique_ptr<AoaPort> m_port;
    std::unique_ptr<AoaTouchSink> m_sink;
    session::SessionPhase m_phase = session::SessionPhase::Idle;
    platform::UsbFailure m_failure = platform::UsbFailure::None;
    AoaState m_portState = AoaState::Starting;
    std::uint16_t m_videoSession = 0;
    QSize m_canvas;
    std::string m_resource;
    std::string m_holder;
};

}
