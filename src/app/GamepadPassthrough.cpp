#include "GamepadPassthrough.h"

#include <gs/input/DeviceEvents.h>

#include <QCoreApplication>
#include <QtLogging>

#include <algorithm>
#include <string>
#include <utility>

namespace gs::app {

namespace {

std::int16_t downward(std::int16_t up) noexcept
{
    return static_cast<std::int16_t>(std::clamp(-static_cast<int>(up), -32768, 32767));
}

std::uint8_t hatOf(std::uint16_t buttons) noexcept
{
    const int vertical = ((buttons & input::xinput::kDpadUp) != 0 ? 1 : 0) -
                         ((buttons & input::xinput::kDpadDown) != 0 ? 1 : 0);
    const int horizontal = ((buttons & input::xinput::kDpadRight) != 0 ? 1 : 0) -
                           ((buttons & input::xinput::kDpadLeft) != 0 ? 1 : 0);
    constexpr std::uint8_t kHats[3][3] = {{5, 4, 3}, {6, input::kPadHatCentred, 2}, {7, 0, 1}};
    return kHats[vertical + 1][horizontal + 1];
}

QString familyName(input::PadFamily family)
{
    switch (family) {
    case input::PadFamily::Xbox:
        return QCoreApplication::translate("GamepadPassthrough", "Xbox Wireless Controller");
    case input::PadFamily::DualShock4:
        return QStringLiteral("DualShock 4");
    case input::PadFamily::DualSense:
        return QStringLiteral("DualSense");
    case input::PadFamily::Generic:
        break;
    }
    return QCoreApplication::translate("GamepadPassthrough", "a generic gamepad");
}

const char* routeName(PadRoute route) noexcept
{
    return route == PadRoute::Aoa ? "AOA" : "UHID";
}

}

input::PadFamily padFamilyOf(platform::PadKind kind) noexcept
{
    switch (kind) {
    case platform::PadKind::Xbox:
        return input::PadFamily::Xbox;
    case platform::PadKind::PlayStation4:
        return input::PadFamily::DualShock4;
    case platform::PadKind::PlayStation5:
        return input::PadFamily::DualSense;
    case platform::PadKind::Other:
        break;
    }
    return input::PadFamily::Generic;
}

input::PadPassState padPassStateOf(const platform::PadState& state) noexcept
{
    if (!state.connected) {
        return {};
    }
    constexpr std::pair<std::uint16_t, std::uint32_t> kButtons[] = {
        {input::xinput::kA, input::pad_button::kSouth},
        {input::xinput::kB, input::pad_button::kEast},
        {input::xinput::kX, input::pad_button::kWest},
        {input::xinput::kY, input::pad_button::kNorth},
        {input::xinput::kLeftShoulder, input::pad_button::kLeftShoulder},
        {input::xinput::kRightShoulder, input::pad_button::kRightShoulder},
        {input::xinput::kBack, input::pad_button::kBack},
        {input::xinput::kStart, input::pad_button::kStart},
        {input::xinput::kLeftThumb, input::pad_button::kLeftStick},
        {input::xinput::kRightThumb, input::pad_button::kRightStick},
    };
    constexpr std::pair<std::uint8_t, std::uint32_t> kExtras[] = {
        {platform::pad_extra::kGuide, input::pad_button::kGuide},
        {platform::pad_extra::kMisc, input::pad_button::kMisc},
        {platform::pad_extra::kTouchpad, input::pad_button::kTouchpad},
    };
    std::uint32_t buttons = 0;
    for (const auto& [bit, button] : kButtons) {
        if ((state.buttons & bit) != 0) {
            buttons |= button;
        }
    }
    for (const auto& [bit, button] : kExtras) {
        if ((state.extra & bit) != 0) {
            buttons |= button;
        }
    }
    if (state.leftTrigger >= input::xinput::kTriggerPress) {
        buttons |= input::pad_button::kLeftTrigger;
    }
    if (state.rightTrigger >= input::xinput::kTriggerPress) {
        buttons |= input::pad_button::kRightTrigger;
    }
    return {.buttons = buttons,
            .hat = hatOf(state.buttons),
            .leftX = state.leftX,
            .leftY = downward(state.leftY),
            .rightX = state.rightX,
            .rightY = downward(state.rightY),
            .leftTrigger = state.leftTrigger,
            .rightTrigger = state.rightTrigger};
}

QString padNoticeText(PadNotice notice, input::PadFamily family)
{
    switch (notice) {
    case PadNotice::Handed:
        if (family == input::PadFamily::Generic) {
            return QCoreApplication::translate("GamepadPassthrough",
                                               "The gamepad goes to the game as a generic gamepad");
        }
        return QCoreApplication::translate("GamepadPassthrough", "The gamepad goes to the game (as %1)")
            .arg(familyName(family));
    case PadNotice::FellBack:
        return QCoreApplication::translate("GamepadPassthrough",
                                           "This phone does not recognise %1; it now gets a generic gamepad "
                                           "without vibration")
            .arg(familyName(family));
    case PadNotice::NeedsUhid:
        return QCoreApplication::translate("GamepadPassthrough",
                                           "This phone cannot take the gamepad directly (it needs UHID)");
    case PadNotice::Unavailable:
        break;
    }
    return QCoreApplication::translate("GamepadPassthrough",
                                       "The gamepad cannot go to the game over this connection yet");
}

GamepadPassthrough::GamepadPassthrough(Callbacks callbacks)
    : m_callbacks(std::move(callbacks))
{
}

void GamepadPassthrough::setExternalSink(std::function<void(const input::PadPassState& state)> sink)
{
    releaseExternal();
    m_external = std::move(sink);
}

void GamepadPassthrough::setTarget(const std::shared_ptr<ControlChannel>& channel, bool passthrough, Microseconds now)
{
    if (channel != m_channel) {
        close(true);
        m_channel = channel;
        m_warned.reset();
        m_announced.reset();
    }
    m_passthrough = passthrough;
    update(now);
}

void GamepadPassthrough::setActive(bool on, Microseconds now)
{
    m_active = on;
    update(now);
}

void GamepadPassthrough::gamepad(const platform::PadState& state, Microseconds now)
{
    m_connected = state.connected;
    if (state.connected) {
        m_pcFamily = padFamilyOf(state.kind);
    }
    m_state = padPassStateOf(state);
    update(now);
}

void GamepadPassthrough::linkChanged(const ControlChannel* channel, Microseconds now)
{
    if (channel != nullptr && channel == m_channel.get()) {
        update(now);
    }
}

void GamepadPassthrough::output(const ControlChannel* channel, std::uint16_t id, std::span<const std::uint8_t> data)
{
    if (!m_device || channel != m_channel.get() || id != m_device->id) {
        return;
    }
    const std::optional<input::PadRumble> rumble = input::padRumble(m_device->family, data);
    if (!rumble) {
        return;
    }
    m_device->rumble = rumble;
    if (m_callbacks.rumble) {
        m_callbacks.rumble(*rumble);
    }
}

void GamepadPassthrough::status(const ControlChannel* channel, std::uint16_t id, bool recognized, bool vibration,
                                Microseconds now)
{
    if (!m_device || channel != m_channel.get() || id != m_device->id || m_device->confirmed) {
        return;
    }
    m_device->confirmed = true;
    const input::PadFamily family = m_device->family;
    if (recognized) {
        if (!vibration && input::padModel(family).rumble) {
            qInfo("gamepad %u: the phone took %s but gave it no vibrator", static_cast<unsigned>(id),
                  std::string(input::padModel(family).name).c_str());
        }
        announce(family);
        return;
    }
    if (family == input::PadFamily::Generic) {
        qWarning("gamepad %u: the phone did not show the generic gamepad either", static_cast<unsigned>(id));
        return;
    }
    qInfo("gamepad %u: the phone does not recognise %s, a generic gamepad takes its place", static_cast<unsigned>(id),
          std::string(input::padModel(family).name).c_str());
    m_refused.emplace_back(m_channel.get(), family);
    close(true);
    tell(PadNotice::FellBack, family);
    update(now);
}

void GamepadPassthrough::forget(const ControlChannel* channel)
{
    std::erase_if(m_refused, [channel](const auto& entry) { return entry.first == channel; });
    if (channel == nullptr || channel != m_channel.get()) {
        return;
    }
    close(true);
    m_channel.reset();
    m_warned.reset();
    m_announced.reset();
}

void GamepadPassthrough::wake(Microseconds now)
{
    if (m_due && now >= *m_due) {
        m_due.reset();
        update(now);
    }
}

std::optional<input::PadFamily> GamepadPassthrough::family() const noexcept
{
    return m_device ? std::optional<input::PadFamily>(m_device->family) : std::nullopt;
}

std::optional<std::uint16_t> GamepadPassthrough::deviceId() const noexcept
{
    return m_device ? std::optional<std::uint16_t>(m_device->id) : std::nullopt;
}

void GamepadPassthrough::update(Microseconds now)
{
    if (!takesPad() || !m_connected) {
        close(true);
        m_warned.reset();
        m_announced.reset();
        return;
    }
    const PadLink link = m_channel->padLink();
    if (m_device && (m_device->route != link.route || m_device->epoch != link.epoch)) {
        close(m_device->epoch == link.epoch);
    }
    if (link.route != PadRoute::External) {
        releaseExternal();
    }
    switch (link.route) {
    case PadRoute::None:
        return;
    case PadRoute::Missing:
        warn(PadNotice::NeedsUhid, link);
        return;
    case PadRoute::External:
        if (!m_external && !m_channel->externalPad()) {
            warn(PadNotice::Unavailable, link);
            return;
        }
        announce(input::PadFamily::Generic);
        sendExternal(now);
        return;
    case PadRoute::Uhid:
    case PadRoute::Aoa:
        break;
    }
    if (!m_device && !open(link)) {
        return;
    }
    send(now);
}

bool GamepadPassthrough::open(const PadLink& link)
{
    const input::PadFamily family =
        link.route == PadRoute::Aoa || refused(m_pcFamily) ? input::PadFamily::Generic : m_pcFamily;
    const input::PadModel& model = input::padModel(family);
    const std::uint16_t id = takeId();
    if (link.route == PadRoute::Uhid) {
        for (const input::PadFeature& feature : model.features) {
            protocol::UhidFeature message;
            message.id = id;
            message.reportNumber = feature.reportNumber;
            message.data.assign(feature.data.begin(), feature.data.end());
            if (!m_channel->padFeature(message)) {
                return false;
            }
        }
    }
    protocol::UhidCreate create;
    create.id = id;
    create.vendorId = model.vendorId;
    create.productId = model.productId;
    create.name = std::string(model.name);
    create.reportDescriptor.assign(model.descriptor.begin(), model.descriptor.end());
    create.bus = model.bus;
    if (!m_channel->padCreate(create)) {
        return false;
    }
    m_device = Device{.id = id,
                      .family = family,
                      .route = link.route,
                      .epoch = link.epoch,
                      .confirmed = link.route != PadRoute::Uhid || !link.reports};
    qInfo("gamepad %u: %s on the phone over %s", static_cast<unsigned>(id), create.name.c_str(), routeName(link.route));
    if (m_device->confirmed) {
        announce(family);
    }
    return true;
}

void GamepadPassthrough::close(bool destroy)
{
    releaseExternal();
    m_due.reset();
    if (!m_device) {
        return;
    }
    const Device device = *m_device;
    m_device.reset();
    if (destroy && m_channel != nullptr) {
        if (device.sent && *device.sent != input::PadPassState{}) {
            protocol::UhidInput released;
            released.id = device.id;
            released.report = input::padInputReport(device.family, {});
            static_cast<void>(m_channel->padInput(released));
        }
        protocol::UhidDestroy message;
        message.id = device.id;
        static_cast<void>(m_channel->padDestroy(message));
        qInfo("gamepad %u: taken off the phone", static_cast<unsigned>(device.id));
    }
    if (device.rumble && *device.rumble != input::PadRumble{} && m_callbacks.rumble) {
        m_callbacks.rumble(input::PadRumble{});
    }
}

void GamepadPassthrough::send(Microseconds now)
{
    const input::PadPassState wanted = shown();
    if (m_device->sent == wanted) {
        m_due.reset();
        return;
    }
    if (!due(now)) {
        return;
    }
    protocol::UhidInput message;
    message.id = m_device->id;
    message.report = input::padInputReport(m_device->family, wanted);
    if (!m_channel->padInput(message)) {
        return;
    }
    m_device->sent = wanted;
    m_lastSent = now;
}

void GamepadPassthrough::sendExternal(Microseconds now)
{
    const input::PadPassState wanted = shown();
    if (m_externalSent == wanted) {
        m_due.reset();
        return;
    }
    if (!due(now)) {
        return;
    }
    if (m_external) {
        m_external(wanted);
    } else if (!m_channel->padExternal(wanted)) {
        return;
    }
    m_externalSent = wanted;
    m_lastSent = now;
}

void GamepadPassthrough::releaseExternal()
{
    if (m_externalSent && *m_externalSent != input::PadPassState{}) {
        if (m_external) {
            m_external(input::PadPassState{});
        } else if (m_channel != nullptr) {
            static_cast<void>(m_channel->padExternal(input::PadPassState{}));
        }
    }
    m_externalSent.reset();
}

void GamepadPassthrough::warn(PadNotice notice, const PadLink& link)
{
    const std::pair<const ControlChannel*, std::uint32_t> key{m_channel.get(), link.epoch};
    if (m_warned == key) {
        return;
    }
    m_warned = key;
    tell(notice, m_pcFamily);
}

void GamepadPassthrough::announce(input::PadFamily family)
{
    const std::pair<const ControlChannel*, input::PadFamily> key{m_channel.get(), family};
    if (m_announced == key) {
        return;
    }
    m_announced = key;
    tell(PadNotice::Handed, family);
}

void GamepadPassthrough::tell(PadNotice notice, input::PadFamily family)
{
    if (m_callbacks.notice) {
        m_callbacks.notice(m_channel.get(), notice, family);
    }
}

bool GamepadPassthrough::refused(input::PadFamily family) const noexcept
{
    return std::ranges::find(m_refused, std::pair<const ControlChannel*, input::PadFamily>{m_channel.get(), family}) !=
           m_refused.end();
}

bool GamepadPassthrough::due(Microseconds now)
{
    if (m_lastSent && now < *m_lastSent + kPadReportInterval) {
        m_due = *m_lastSent + kPadReportInterval;
        return false;
    }
    m_due.reset();
    return true;
}

input::PadPassState GamepadPassthrough::shown() const noexcept
{
    return m_active ? m_state : input::PadPassState{};
}

std::uint16_t GamepadPassthrough::takeId() noexcept
{
    const std::uint16_t id = m_nextId;
    m_nextId = m_nextId >= kPadIdLast ? kPadIdFirst : static_cast<std::uint16_t>(m_nextId + 1);
    return id;
}

}
