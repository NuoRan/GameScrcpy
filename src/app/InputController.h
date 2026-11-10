#pragma once

#include "ControlChannel.h"
#include "InputHost.h"

#include <gs/input/Profile.h>
#include <gs/platform/Cursor.h>
#include <gs/platform/InputCapture.h>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

struct SyncFollower {
    QString serial;
    std::shared_ptr<ControlChannel> channel;
    std::shared_ptr<const input::Profile> profile;

    friend bool operator==(const SyncFollower&, const SyncFollower&) = default;
};

class InputController final : public QObject
{
    Q_OBJECT

public:
    explicit InputController(QObject* parent = nullptr);
    ~InputController() override;

    [[nodiscard]] bool keymap() const noexcept { return m_keymap; }
    [[nodiscard]] bool hasProfile() const noexcept { return m_hasProfile; }
    [[nodiscard]] bool cameraLocked() const noexcept { return m_locked; }
    [[nodiscard]] bool cursorPeeking() const noexcept { return m_peeking; }
    [[nodiscard]] bool textInput() const noexcept { return m_textInput; }
    [[nodiscard]] const QString& target() const noexcept { return m_target; }
    [[nodiscard]] bool engaged() const noexcept { return m_engaged; }
    [[nodiscard]] bool binds(input::InputSource source) const;
    [[nodiscard]] QString problem() const;

    void setTarget(const QString& serial, std::shared_ptr<ControlChannel> channel, std::uintptr_t window);
    void setPictureArea(const QString& serial, const QRectF& share);
    void cursor(const QString& serial, std::shared_ptr<ControlChannel> channel, std::uintptr_t window,
                input::CursorPhase phase, input::Vec2 position);
    void setProfile(std::shared_ptr<const input::Profile> profile, bool revision = false);
    void setKeymap(bool on);
    void setCameraLock(input::Switch action);
    void setTextInput(bool on);
    void setFocused(bool focused);
    [[nodiscard]] std::optional<input::Vec2> cursorOver(const QString& serial) const;
    [[nodiscard]] bool heldOn(const QString& serial, std::string_view name) const;
    void attachScript(const QString& serial, const std::shared_ptr<ControlChannel>& channel,
                      std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer);
    void detachScript(const std::shared_ptr<ControlChannel>& channel, const std::shared_ptr<LaneObserver>& observer);
    void claimScriptKeys(const QString& serial, const std::shared_ptr<ControlChannel>& channel,
                         std::vector<input::InputSource> keys);
    void handOverScriptControls(const std::shared_ptr<ControlChannel>& channel, std::vector<std::string> ids);
    void watchScript(const std::shared_ptr<ControlChannel>& channel, std::shared_ptr<const input::Profile> profile,
                     std::shared_ptr<LaneObserver> observer);
    void unwatchScript(const std::shared_ptr<ControlChannel>& channel, const std::shared_ptr<LaneObserver>& observer);
    void watchScriptKeys(const std::shared_ptr<ControlChannel>& channel, const std::shared_ptr<LaneObserver>& observer,
                         std::vector<input::InputSource> keys);
    void forget(const std::shared_ptr<ControlChannel>& channel);
    void setFollowers(const QString& master, const std::shared_ptr<ControlChannel>& channel,
                      const std::vector<SyncFollower>& followers);
    void post(std::function<void(InputHost& host, Microseconds now)> task);
    void padLinkChanged(const std::shared_ptr<ControlChannel>& channel);
    void padOutput(const std::shared_ptr<ControlChannel>& channel, std::uint16_t id, const QByteArray& data);
    void padStatus(const std::shared_ptr<ControlChannel>& channel, std::uint16_t id, bool recognized, bool vibration);

signals:
    void keymapChanged(bool on);
    void cameraLockChanged(bool locked);
    void cursorPeekChanged(bool on);
    void actionWheelChanged(const QString& control, bool open, int cell);
    void textInputChanged(bool on);
    void noCamera();
    void controlsLiveChanged(const QString& serial, const QList<bool>& live, quintptr profile);
    void controlsAimingChanged(const QString& serial, const QList<bool>& aiming, quintptr profile);
    void gamepadNotice(const QString& serial, const QString& text);

private:
    void apply();
    void lockChanged(bool locked);
    void updateCursorTarget();
    [[nodiscard]] std::optional<input::Vec2> locateCursor() const;
    [[nodiscard]] QString serialOf(const ControlChannel* channel) const;

    mutable std::mutex m_cursorMutex;
    std::uintptr_t m_cursorWindow = 0;
    QString m_cursorTarget;
    bool m_cursorFocused = false;
    std::optional<platform::AreaShare> m_cursorShare;
    mutable std::mutex m_keysMutex;
    std::map<QString, std::vector<input::InputSource>> m_scriptKeys;
    std::map<QString, QRectF> m_pictures;
    std::map<const ControlChannel*, QString> m_serials;
    InputHost m_host;
    platform::InputCapture m_capture;
    QString m_target;
    std::shared_ptr<ControlChannel> m_channel;
    std::uintptr_t m_window = 0;
    std::uintptr_t m_captureWindow = 0;
    std::vector<input::InputSource> m_bound;
    bool m_keymap = true;
    bool m_hasProfile = false;
    bool m_locked = false;
    bool m_peeking = false;
    bool m_textInput = false;
    bool m_focused = false;
    bool m_engaged = false;
};

}
