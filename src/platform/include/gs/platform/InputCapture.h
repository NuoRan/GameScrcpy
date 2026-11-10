#pragma once

#include <gs/base/Clock.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace gs::platform {

struct RawKey {
    std::uint16_t makeCode = 0;
    bool e0 = false;
    bool e1 = false;
    bool released = false;

    friend bool operator==(const RawKey&, const RawKey&) = default;
};

struct RawMouse {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::uint16_t buttonFlags = 0;
    std::int16_t buttonData = 0;

    friend bool operator==(const RawMouse&, const RawMouse&) = default;
};

enum class PadKind : std::uint8_t { Other, Xbox, PlayStation4, PlayStation5 };

namespace pad_extra {
inline constexpr std::uint8_t kGuide = 0x01;
inline constexpr std::uint8_t kMisc = 0x02;
inline constexpr std::uint8_t kTouchpad = 0x04;
}

struct PadState {
    bool connected = false;
    std::uint16_t buttons = 0;
    std::uint8_t leftTrigger = 0;
    std::uint8_t rightTrigger = 0;
    std::int16_t leftX = 0;
    std::int16_t leftY = 0;
    std::int16_t rightX = 0;
    std::int16_t rightY = 0;
    std::uint8_t extra = 0;
    PadKind kind = PadKind::Other;

    friend bool operator==(const PadState&, const PadState&) = default;
};

class InputHandler
{
public:
    virtual ~InputHandler() = default;

    virtual void key(const RawKey& raw, Microseconds now) = 0;
    virtual void mouse(const RawMouse& report, Microseconds now) = 0;
    virtual void gamepad(const PadState& state, Microseconds now) = 0;
    virtual void wake(Microseconds now) = 0;
    [[nodiscard]] virtual std::optional<Microseconds> nextWakeup() const = 0;
};

class InputCapture
{
public:
    static constexpr Microseconds kDefaultPollInterval{4000};

    explicit InputCapture(InputHandler& handler, Microseconds pollInterval = kDefaultPollInterval);
    ~InputCapture();
    InputCapture(const InputCapture&) = delete;
    InputCapture& operator=(const InputCapture&) = delete;

    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] std::string problem() const;
    [[nodiscard]] static Microseconds now() noexcept;

    void post(std::function<void()> task);
    void setCaptureTarget(std::uintptr_t window);
    void rumble(std::uint16_t low, std::uint16_t high, std::uint32_t durationMs);
    void rumbleTriggers(std::uint16_t left, std::uint16_t right, std::uint32_t durationMs);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
