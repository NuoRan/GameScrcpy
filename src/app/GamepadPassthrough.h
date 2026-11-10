#pragma once

#include "ControlChannel.h"

#include <gs/base/Clock.h>
#include <gs/input/PadReports.h>
#include <gs/platform/InputCapture.h>

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace gs::app {

enum class PadNotice : std::uint8_t { Handed, FellBack, NeedsUhid, Unavailable };

inline constexpr std::uint16_t kPadIdFirst = 0x0100;
inline constexpr std::uint16_t kPadIdLast = 0x01FF;
inline constexpr Microseconds kPadReportInterval{4000};
inline constexpr std::uint32_t kPadRumbleMs = 0xFFFF;

[[nodiscard]] input::PadFamily padFamilyOf(platform::PadKind kind) noexcept;
[[nodiscard]] input::PadPassState padPassStateOf(const platform::PadState& state) noexcept;
[[nodiscard]] QString padNoticeText(PadNotice notice, input::PadFamily family);

class GamepadPassthrough
{
public:
    struct Callbacks {
        std::function<void(const input::PadRumble& rumble)> rumble;
        std::function<void(const ControlChannel* channel, PadNotice notice, input::PadFamily family)> notice;
    };

    explicit GamepadPassthrough(Callbacks callbacks = {});

    void setExternalSink(std::function<void(const input::PadPassState& state)> sink);
    void setTarget(const std::shared_ptr<ControlChannel>& channel, bool passthrough, Microseconds now);
    void setActive(bool on, Microseconds now);
    void gamepad(const platform::PadState& state, Microseconds now);
    void linkChanged(const ControlChannel* channel, Microseconds now);
    void output(const ControlChannel* channel, std::uint16_t id, std::span<const std::uint8_t> data);
    void status(const ControlChannel* channel, std::uint16_t id, bool recognized, bool vibration, Microseconds now);
    void forget(const ControlChannel* channel);
    void wake(Microseconds now);

    [[nodiscard]] bool takesPad() const noexcept { return m_channel != nullptr && m_passthrough; }
    [[nodiscard]] std::optional<Microseconds> nextWakeup() const noexcept { return m_due; }
    [[nodiscard]] std::optional<input::PadFamily> family() const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> deviceId() const noexcept;

private:
    struct Device {
        std::uint16_t id = 0;
        input::PadFamily family = input::PadFamily::Generic;
        PadRoute route = PadRoute::None;
        std::uint32_t epoch = 0;
        bool confirmed = false;
        std::optional<input::PadPassState> sent;
        std::optional<input::PadRumble> rumble;
    };

    void update(Microseconds now);
    [[nodiscard]] bool open(const PadLink& link);
    void close(bool destroy);
    void send(Microseconds now);
    void sendExternal(Microseconds now);
    void releaseExternal();
    void warn(PadNotice notice, const PadLink& link);
    void announce(input::PadFamily family);
    void tell(PadNotice notice, input::PadFamily family);
    [[nodiscard]] bool refused(input::PadFamily family) const noexcept;
    [[nodiscard]] bool due(Microseconds now);
    [[nodiscard]] input::PadPassState shown() const noexcept;
    [[nodiscard]] std::uint16_t takeId() noexcept;

    Callbacks m_callbacks;
    std::function<void(const input::PadPassState&)> m_external;
    std::shared_ptr<ControlChannel> m_channel;
    bool m_passthrough = false;
    bool m_active = false;
    bool m_connected = false;
    input::PadFamily m_pcFamily = input::PadFamily::Generic;
    input::PadPassState m_state;
    std::optional<Device> m_device;
    std::optional<input::PadPassState> m_externalSent;
    std::optional<Microseconds> m_lastSent;
    std::optional<Microseconds> m_due;
    std::uint16_t m_nextId = kPadIdFirst;
    std::optional<std::pair<const ControlChannel*, std::uint32_t>> m_warned;
    std::optional<std::pair<const ControlChannel*, input::PadFamily>> m_announced;
    std::vector<std::pair<const ControlChannel*, input::PadFamily>> m_refused;
};

}
