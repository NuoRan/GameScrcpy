#include "InputController.h"

#include <gs/input/DeviceEvents.h>
#include <gs/input/InputSource.h>
#include <gs/platform/Foreground.h>

#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace gs::app {

InputController::InputController(QObject* parent)
    : QObject(parent)
    , m_host({.pointer =
                  [this](input::PointerState state) {
                      QMetaObject::invokeMethod(
                          this, [this, state] { lockChanged(state == input::PointerState::Captured); },
                          Qt::QueuedConnection);
                  },
              .notice =
                  [this](input::EngineNotice notice) {
                      if (notice == input::EngineNotice::NoCamera) {
                          QMetaObject::invokeMethod(this, [this] { emit noCamera(); }, Qt::QueuedConnection);
                      }
                  },
              .live =
                  [this](const ControlChannel* channel, std::vector<bool> live, std::uintptr_t profile) {
                      QMetaObject::invokeMethod(
                          this,
                          [this, channel, flags = QList<bool>(live.begin(), live.end()), profile] {
                              const QString serial = serialOf(channel);
                              if (!serial.isEmpty()) {
                                  emit controlsLiveChanged(serial, flags, static_cast<quintptr>(profile));
                              }
                          },
                          Qt::QueuedConnection);
                  },
              .cursor = [this] { return locateCursor(); },
              .aiming =
                  [this](const ControlChannel* channel, std::vector<bool> aiming, std::uintptr_t profile) {
                      QMetaObject::invokeMethod(
                          this,
                          [this, channel, flags = QList<bool>(aiming.begin(), aiming.end()), profile] {
                              const QString serial = serialOf(channel);
                              if (!serial.isEmpty()) {
                                  emit controlsAimingChanged(serial, flags, static_cast<quintptr>(profile));
                              }
                          },
                          Qt::QueuedConnection);
                  },
              .peek =
                  [this](bool on) {
                      QMetaObject::invokeMethod(
                          this,
                          [this, on] {
                              if (std::exchange(m_peeking, on) != on) {
                                  emit cursorPeekChanged(on);
                              }
                          },
                          Qt::QueuedConnection);
                  },
              .wheel =
                  [this](const std::string& control, bool open, int cell) {
                      QMetaObject::invokeMethod(
                          this,
                          [this, name = QString::fromStdString(control), open, cell] {
                              emit actionWheelChanged(name, open, cell);
                          },
                          Qt::QueuedConnection);
                  },
              .rumble =
                  [this](const input::PadRumble& rumble) {
                      m_capture.rumble(rumble.strong, rumble.weak, kPadRumbleMs);
                      m_capture.rumbleTriggers(rumble.leftTrigger, rumble.rightTrigger, kPadRumbleMs);
                  },
              .padNotice =
                  [this](const ControlChannel* channel, PadNotice notice, input::PadFamily family) {
                      QMetaObject::invokeMethod(
                          this,
                          [this, channel, notice, family] {
                              const QString serial = serialOf(channel);
                              if (!serial.isEmpty()) {
                                  emit gamepadNotice(serial, padNoticeText(notice, family));
                              }
                          },
                          Qt::QueuedConnection);
                  }})
    , m_capture(m_host)
{
    if (!m_capture.started()) {
        qWarning("input capture not available: %s", m_capture.problem().c_str());
    }
}

InputController::~InputController() = default;

QString InputController::problem() const
{
    return QString::fromStdString(m_capture.problem());
}

void InputController::setTarget(const QString& serial, std::shared_ptr<ControlChannel> channel, std::uintptr_t window)
{
    if (serial != m_target) {
        setTextInput(false);
    }
    m_target = serial;
    m_window = window;
    updateCursorTarget();
    if (channel != nullptr) {
        m_serials[channel.get()] = serial;
    }
    if (channel != m_channel) {
        m_channel = std::move(channel);
        m_capture.post([host = &m_host, next = m_channel] { host->setChannel(next, platform::InputCapture::now()); });
    }
    apply();
}

void InputController::cursor(const QString& serial, std::shared_ptr<ControlChannel> channel, std::uintptr_t window,
                             input::CursorPhase phase, input::Vec2 position)
{
    setTarget(serial, std::move(channel), window);
    m_capture.post([host = &m_host, phase, position] { host->cursor(phase, position, platform::InputCapture::now()); });
}

void InputController::setPictureArea(const QString& serial, const QRectF& share)
{
    m_pictures[serial] = share;
    if (serial == m_target) {
        updateCursorTarget();
    }
}

void InputController::updateCursorTarget()
{
    const auto picture = m_pictures.find(m_target);
    const std::scoped_lock lock(m_cursorMutex);
    m_cursorWindow = m_window;
    m_cursorTarget = m_target;
    m_cursorShare.reset();
    if (picture != m_pictures.end() && !picture->second.isEmpty()) {
        m_cursorShare = platform::AreaShare{.left = picture->second.x(),
                                            .top = picture->second.y(),
                                            .width = picture->second.width(),
                                            .height = picture->second.height()};
    }
}

std::optional<input::Vec2> InputController::locateCursor() const
{
    std::uintptr_t window = 0;
    platform::AreaShare share;
    {
        const std::scoped_lock lock(m_cursorMutex);
        if (!m_cursorShare) {
            return std::nullopt;
        }
        window = m_cursorWindow;
        share = *m_cursorShare;
    }
    const std::optional<platform::ScreenArea> client = platform::clientArea(window);
    const std::optional<platform::ScreenPoint> cursor = platform::cursorPosition();
    if (!client || !cursor) {
        return std::nullopt;
    }
    const double width = share.width * client->width;
    const double height = share.height * client->height;
    const input::Vec2 position{(cursor->x - (client->x + share.left * client->width)) / width,
                               (cursor->y - (client->y + share.top * client->height)) / height};
    if (position.x < 0.0 || position.x > 1.0 || position.y < 0.0 || position.y > 1.0) {
        return std::nullopt;
    }
    return position;
}

bool InputController::binds(input::InputSource source) const
{
    if (std::binary_search(m_bound.begin(), m_bound.end(), source)) {
        return true;
    }
    const std::scoped_lock lock(m_keysMutex);
    const auto found = m_scriptKeys.find(m_target);
    return found != m_scriptKeys.end() && std::binary_search(found->second.begin(), found->second.end(), source);
}

void InputController::setProfile(std::shared_ptr<const input::Profile> profile, bool revision)
{
    m_hasProfile = profile != nullptr;
    m_bound.clear();
    if (m_hasProfile) {
        for (const input::Chord& chord : input::boundInputs(*profile)) {
            m_bound.push_back(chord.source);
        }
        m_bound.push_back(defaultLockKey().source);
        std::sort(m_bound.begin(), m_bound.end());
        m_bound.erase(std::unique(m_bound.begin(), m_bound.end()), m_bound.end());
    }
    if (!m_hasProfile) {
        setTextInput(false);
    }
    m_capture.post([host = &m_host, next = std::move(profile), revision] {
        host->setProfile(next, platform::InputCapture::now(), revision);
    });
    apply();
}

void InputController::setKeymap(bool on)
{
    if (on == m_keymap) {
        return;
    }
    m_keymap = on;
    if (!on) {
        setTextInput(false);
    }
    apply();
    if (!on) {
        setCameraLock(input::Switch::Off);
    }
    emit keymapChanged(on);
}

void InputController::setCameraLock(input::Switch action)
{
    m_capture.post([host = &m_host, action] { host->setCameraLock(action, platform::InputCapture::now()); });
}

void InputController::setTextInput(bool on)
{
    const bool next = on && m_keymap && m_hasProfile;
    if (next == m_textInput) {
        return;
    }
    m_textInput = next;
    apply();
    if (next) {
        setCameraLock(input::Switch::Off);
    }
    emit textInputChanged(next);
}

void InputController::setFocused(bool focused)
{
    if (focused == m_focused) {
        return;
    }
    m_focused = focused;
    {
        const std::scoped_lock lock(m_cursorMutex);
        m_cursorFocused = focused;
    }
    apply();
}

std::optional<input::Vec2> InputController::cursorOver(const QString& serial) const
{
    {
        const std::scoped_lock lock(m_cursorMutex);
        if (!m_cursorFocused || m_cursorTarget != serial) {
            return std::nullopt;
        }
    }
    const std::optional<input::Vec2> at = locateCursor();
    if (!at || at->x < 0.0 || at->x > 1.0 || at->y < 0.0 || at->y > 1.0) {
        return std::nullopt;
    }
    return at;
}

bool InputController::heldOn(const QString& serial, std::string_view name) const
{
    {
        const std::scoped_lock lock(m_cursorMutex);
        if (!m_cursorFocused || m_cursorTarget != serial) {
            return false;
        }
    }
    const std::optional<input::Chord> chord = input::parseChord(name);
    if (!chord) {
        return false;
    }
    const auto modifierHeld = [](std::string_view left, std::string_view right) {
        const auto scan = [](std::string_view key) {
            const std::optional<input::InputSource> source = input::parseSource(key);
            const std::optional<std::uint16_t> code = source ? input::scanCodeOf(*source) : std::nullopt;
            return code && platform::scanCodeHeld(*code);
        };
        return scan(left) || scan(right);
    };
    if (((chord->modifiers & input::modifier::kCtrl) != 0 && !modifierHeld("ControlLeft", "ControlRight")) ||
        ((chord->modifiers & input::modifier::kShift) != 0 && !modifierHeld("ShiftLeft", "ShiftRight")) ||
        ((chord->modifiers & input::modifier::kAlt) != 0 && !modifierHeld("AltLeft", "AltRight"))) {
        return false;
    }
    if (chord->source.device == input::Device::Keyboard) {
        const std::optional<std::uint16_t> code = input::scanCodeOf(chord->source);
        return code && platform::scanCodeHeld(*code);
    }
    if (chord->source.device == input::Device::Mouse) {
        constexpr std::pair<std::string_view, std::uint8_t> kButtons[] = {
            {"Mouse.Left", platform::mouse_button::kLeft},   {"Mouse.Right", platform::mouse_button::kRight},
            {"Mouse.Middle", platform::mouse_button::kMiddle}, {"Mouse.X1", platform::mouse_button::kX1},
            {"Mouse.X2", platform::mouse_button::kX2}};
        for (const auto& [button, bit] : kButtons) {
            if (input::parseSource(button) == chord->source) {
                return (platform::pressedMouseButtons() & bit) != 0;
            }
        }
    }
    return false;
}

void InputController::attachScript(const QString& serial, const std::shared_ptr<ControlChannel>& channel,
                                   std::shared_ptr<const input::Profile> profile,
                                   std::shared_ptr<LaneObserver> observer)
{
    if (channel == nullptr || observer == nullptr) {
        return;
    }
    m_serials[channel.get()] = serial;
    m_capture.post([host = &m_host, channel, profile = std::move(profile), observer = std::move(observer)] {
        host->attach(channel, profile, observer, platform::InputCapture::now());
    });
}

void InputController::detachScript(const std::shared_ptr<ControlChannel>& channel,
                                   const std::shared_ptr<LaneObserver>& observer)
{
    if (channel == nullptr || observer == nullptr) {
        return;
    }
    m_capture.post([host = &m_host, channel, observer] {
        host->detach(channel.get(), observer.get(), platform::InputCapture::now());
    });
}

void InputController::claimScriptKeys(const QString& serial, const std::shared_ptr<ControlChannel>& channel,
                                      std::vector<input::InputSource> keys)
{
    if (channel == nullptr) {
        return;
    }
    std::ranges::sort(keys);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    {
        const std::scoped_lock lock(m_keysMutex);
        if (keys.empty()) {
            m_scriptKeys.erase(serial);
        } else {
            m_scriptKeys[serial] = keys;
        }
    }
    m_capture.post([host = &m_host, channel, keys = std::move(keys)]() mutable {
        host->claimKeys(channel.get(), std::move(keys));
    });
}

void InputController::handOverScriptControls(const std::shared_ptr<ControlChannel>& channel,
                                             std::vector<std::string> ids)
{
    if (channel == nullptr) {
        return;
    }
    m_capture.post([host = &m_host, channel, ids = std::move(ids)]() mutable {
        host->handOver(channel.get(), std::move(ids));
    });
}

void InputController::watchScript(const std::shared_ptr<ControlChannel>& channel,
                                  std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer)
{
    if (channel == nullptr || observer == nullptr) {
        return;
    }
    m_capture.post([host = &m_host, channel, profile = std::move(profile), observer = std::move(observer)] {
        host->watch(channel, profile, observer, platform::InputCapture::now());
    });
}

void InputController::unwatchScript(const std::shared_ptr<ControlChannel>& channel,
                                    const std::shared_ptr<LaneObserver>& observer)
{
    if (channel == nullptr || observer == nullptr) {
        return;
    }
    m_capture.post([host = &m_host, channel, observer] {
        host->unwatch(channel.get(), observer.get(), platform::InputCapture::now());
    });
}

void InputController::watchScriptKeys(const std::shared_ptr<ControlChannel>& channel,
                                      const std::shared_ptr<LaneObserver>& observer, std::vector<input::InputSource> keys)
{
    if (channel == nullptr || observer == nullptr) {
        return;
    }
    m_capture.post([host = &m_host, channel, observer, keys = std::move(keys)]() mutable {
        host->watchKeys(channel.get(), observer.get(), std::move(keys));
    });
}

void InputController::forget(const std::shared_ptr<ControlChannel>& channel)
{
    if (channel == nullptr) {
        return;
    }
    m_serials.erase(channel.get());
    m_capture.post([host = &m_host, channel] { host->forget(channel.get(), platform::InputCapture::now()); });
}

void InputController::setFollowers(const QString& master, const std::shared_ptr<ControlChannel>& channel,
                                   const std::vector<SyncFollower>& followers)
{
    if (channel == nullptr) {
        return;
    }
    m_serials[channel.get()] = master;
    std::vector<InputHost::Follower> lanes;
    lanes.reserve(followers.size());
    for (const SyncFollower& follower : followers) {
        if (follower.channel == nullptr) {
            continue;
        }
        m_serials[follower.channel.get()] = follower.serial;
        lanes.push_back({.channel = follower.channel, .profile = follower.profile});
    }
    m_capture.post([host = &m_host, channel, lanes = std::move(lanes)] {
        static_cast<void>(host->setFollowers(channel, lanes, platform::InputCapture::now()));
    });
}

void InputController::post(std::function<void(InputHost& host, Microseconds now)> task)
{
    m_capture.post([host = &m_host, task = std::move(task)] { task(*host, platform::InputCapture::now()); });
}

void InputController::padLinkChanged(const std::shared_ptr<ControlChannel>& channel)
{
    if (channel == nullptr) {
        return;
    }
    m_capture.post([host = &m_host, channel] { host->padLinkChanged(channel.get(), platform::InputCapture::now()); });
}

void InputController::padOutput(const std::shared_ptr<ControlChannel>& channel, std::uint16_t id,
                                const QByteArray& data)
{
    if (channel == nullptr) {
        return;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.constData());
    m_capture.post([host = &m_host, channel, id, report = std::vector<std::uint8_t>(bytes, bytes + data.size())] {
        host->padOutput(channel.get(), id, report);
    });
}

void InputController::padStatus(const std::shared_ptr<ControlChannel>& channel, std::uint16_t id, bool recognized,
                                bool vibration)
{
    if (channel == nullptr) {
        return;
    }
    m_capture.post([host = &m_host, channel, id, recognized, vibration] {
        host->padStatus(channel.get(), id, recognized, vibration, platform::InputCapture::now());
    });
}

QString InputController::serialOf(const ControlChannel* channel) const
{
    const auto found = m_serials.find(channel);
    return found != m_serials.end() ? found->second : QString();
}

void InputController::lockChanged(bool locked)
{
    if (locked == m_locked) {
        return;
    }
    m_locked = locked;
    emit cameraLockChanged(locked);
}

void InputController::apply()
{
    const bool engaged =
        m_keymap && m_hasProfile && !m_textInput && m_focused && m_channel != nullptr && m_window != 0;
    if (engaged != m_engaged) {
        m_engaged = engaged;
        m_capture.post([host = &m_host, engaged] { host->setActive(engaged, platform::InputCapture::now()); });
    }
    const std::uintptr_t window = engaged ? m_window : 0;
    if (window != m_captureWindow) {
        m_captureWindow = window;
        m_capture.setCaptureTarget(window);
    }
}

}
