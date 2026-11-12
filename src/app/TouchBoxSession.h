#pragma once

#include "ControlChannel.h"
#include "TouchMethods.h"

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

class TouchBoxSink;

class TouchBoxSession final : public QObject
{
    Q_OBJECT

public:
    TouchBoxSession(QString name, std::string method, QObject* parent = nullptr);
    ~TouchBoxSession() override;
    TouchBoxSession(const TouchBoxSession&) = delete;
    TouchBoxSession& operator=(const TouchBoxSession&) = delete;

    void start(TouchMethodSource* source, const std::string& device, int rotation, QSize canvas);
    void stop();
    void setOrientation(int rotation, QSize canvas);
    void pressKey(std::uint32_t keycode);
    void moveMouse(int dx, int dy);
    void setMouseButtons(int buttons);

    [[nodiscard]] session::SessionPhase phase() const noexcept { return m_phase; }
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] int rotation() const noexcept { return m_rotation; }
    [[nodiscard]] QSize canvas() const noexcept { return m_canvas; }
    [[nodiscard]] const std::shared_ptr<ControlChannel>& channel() const noexcept { return m_channel; }
    [[nodiscard]] const std::shared_ptr<render::FrameSlot>& frames() const noexcept { return m_frames; }

signals:
    void stateChanged(const gs::session::SessionState& state);
    void padLinkChanged();

private:
    void fellBack(std::uint32_t generation);
    void enter(session::SessionPhase phase, std::optional<session::SessionFailure> failure = std::nullopt);
    void release();

    const QString m_name;
    const std::string m_method;
    const std::shared_ptr<ControlChannel> m_channel = std::make_shared<ControlChannel>();
    const std::shared_ptr<render::FrameSlot> m_frames = std::make_shared<render::FrameSlot>();
    std::unique_ptr<ControlSink> m_quiet;
    std::unique_ptr<ExtensionTouch> m_touch;
    std::unique_ptr<TouchBoxSink> m_sink;
    session::SessionPhase m_phase = session::SessionPhase::Idle;
    std::uint32_t m_generation = 0;
    std::uint32_t m_padEpoch = 0;
    std::uint16_t m_videoSession = 0;
    int m_rotation = 0;
    QSize m_canvas;
};

}
