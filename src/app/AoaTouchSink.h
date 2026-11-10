#pragma once

#include "ControlChannel.h"
#include "HidTouchscreen.h"

#include <gs/protocol/Messages.h>
#include <gs/session/SessionState.h>

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace gs::platform {
struct UsbPhone;
enum class UsbFailure : std::uint8_t;
}

namespace gs::app {

namespace aoa {
inline constexpr std::uint8_t kGetProtocol = 51;
inline constexpr std::uint8_t kRegisterHid = 54;
inline constexpr std::uint8_t kUnregisterHid = 55;
inline constexpr std::uint8_t kSetHidReportDescriptor = 56;
inline constexpr std::uint8_t kSendHidEvent = 57;
inline constexpr std::uint16_t kTouchscreen = 1;
inline constexpr std::uint16_t kMouse = 2;
inline constexpr std::uint16_t kKeyboard = 3;
inline constexpr std::uint16_t kGamepad = 4;
inline constexpr std::uint16_t kLeastProtocol = 2;
inline constexpr std::size_t kMouseReportSize = 7;
inline constexpr std::size_t kKeyboardReportSize = 9;
inline constexpr std::size_t kConsumerReportSize = 3;
inline constexpr std::uint8_t kKeyboardReport = 1;
inline constexpr std::uint8_t kConsumerReport = 2;
inline constexpr std::size_t kMostKeys = 6;
inline constexpr std::size_t kMostQueued = 512;
}

[[nodiscard]] std::span<const std::uint8_t> aoaMouseDescriptor() noexcept;
[[nodiscard]] std::span<const std::uint8_t> aoaKeyboardDescriptor() noexcept;

struct HidKey {
    std::uint8_t report = 0;
    std::uint16_t usage = 0;

    friend bool operator==(const HidKey&, const HidKey&) = default;
};

[[nodiscard]] std::optional<HidKey> hidKeyOf(std::uint32_t androidKeycode) noexcept;

class AoaLink
{
public:
    virtual ~AoaLink() = default;

    [[nodiscard]] virtual std::optional<std::uint16_t> protocol() = 0;
    [[nodiscard]] virtual bool send(std::uint8_t request, std::uint16_t value, std::uint16_t index,
                                    std::span<const std::uint8_t> data) = 0;
    [[nodiscard]] virtual std::size_t packetSize() const = 0;
    [[nodiscard]] virtual bool gone() const = 0;
};

[[nodiscard]] std::unique_ptr<AoaLink> openAoaLink(const platform::UsbPhone& phone, platform::UsbFailure& failure);

enum class AoaState : std::uint8_t { Starting, Ready, Unsupported, Failed, Gone };

class AoaPort
{
public:
    enum class Merge : std::uint8_t { None, Touch, Motion };
    using Listener = std::function<void(AoaState state)>;

    explicit AoaPort(std::unique_ptr<AoaLink> link, Listener listener = {});
    ~AoaPort();
    AoaPort(const AoaPort&) = delete;
    AoaPort& operator=(const AoaPort&) = delete;

    [[nodiscard]] bool add(std::uint16_t id, std::span<const std::uint8_t> descriptor);
    [[nodiscard]] bool event(std::uint16_t id, std::span<const std::uint8_t> report, Merge merge = Merge::None);
    [[nodiscard]] bool remove(std::uint16_t id);
    void flush();
    [[nodiscard]] AoaState state() const;
    [[nodiscard]] int failedEvents() const;

private:
    enum class Kind : std::uint8_t { Register, Event, Unregister };

    struct Item {
        Kind kind = Kind::Event;
        std::uint16_t id = 0;
        std::vector<std::uint8_t> bytes;
        Merge merge = Merge::None;
    };

    [[nodiscard]] bool queue(Item item);
    [[nodiscard]] static bool merged(Item& pending, const Item& next);
    void run(const std::stop_token& stop);
    [[nodiscard]] bool perform(const Item& item);
    void publish(AoaState state);

    std::unique_ptr<AoaLink> m_link;
    Listener m_listener;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::condition_variable_any m_drained;
    std::deque<Item> m_queue;
    bool m_idle = false;
    AoaState m_state = AoaState::Starting;
    int m_failedEvents = 0;
    std::jthread m_thread;
};

struct AoaDevices {
    bool mouse = false;
    bool keyboard = false;
};

class AoaTouchSink final : public ControlSink
{
public:
    AoaTouchSink(ControlSink* injection, AoaPort& port, AoaDevices devices);
    ~AoaTouchSink() override;
    AoaTouchSink(const AoaTouchSink&) = delete;
    AoaTouchSink& operator=(const AoaTouchSink&) = delete;

    [[nodiscard]] bool send(const protocol::TouchFrame& frame) override;
    [[nodiscard]] bool send(const protocol::Key& key) override;
    [[nodiscard]] bool send(const protocol::Scroll& scroll) override;
    [[nodiscard]] bool send(const protocol::Text& text) override;
    [[nodiscard]] SinkCapabilities capabilities() const override;
    void detached() override;
    [[nodiscard]] bool padFeature(const protocol::UhidFeature& feature) override;
    [[nodiscard]] bool padCreate(const protocol::UhidCreate& create) override;
    [[nodiscard]] bool padInput(const protocol::UhidInput& input) override;
    [[nodiscard]] bool padDestroy(const protocol::UhidDestroy& destroy) override;

    void phaseChanged(session::SessionPhase phase);
    void setVideo(std::uint16_t session, int rotation);
    bool moveMouse(int dx, int dy);
    bool setMouseButtons(std::uint8_t buttons);
    bool scrollMouse(int vertical, int horizontal);
    bool pressKey(std::uint32_t keycode);
    void releaseAll();

    [[nodiscard]] int ignoredContacts() const;
    [[nodiscard]] bool attached() const;

private:
    [[nodiscard]] bool touch(const TouchReport& report);
    [[nodiscard]] bool key(std::uint32_t keycode, bool down);
    [[nodiscard]] bool keyboardReport();
    [[nodiscard]] bool consumerReport();
    [[nodiscard]] bool mouseReport(int dx, int dy, int vertical, int horizontal, AoaPort::Merge merge);
    void releaseLocked();
    void detachLocked();

    mutable std::mutex m_mutex;
    ControlSink* m_injection;
    AoaPort& m_port;
    AoaDevices m_devices;
    bool m_attached = false;
    TouchscreenEncoder m_encoder;
    std::optional<TouchReport> m_lastReport;
    std::uint16_t m_videoSession = 0;
    int m_rotation = 0;
    std::uint8_t m_buttons = 0;
    std::uint8_t m_modifiers = 0;
    std::array<std::uint8_t, aoa::kMostKeys> m_keys{};
    std::uint16_t m_consumer = 0;
    bool m_padAdded = false;
    std::uint16_t m_padId = 0;
    bool m_padNumbered = false;
    std::uint8_t m_padReportId = 0;
    std::size_t m_padReportSize = 0;
};

}
