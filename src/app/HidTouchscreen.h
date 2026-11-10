#pragma once

#include <gs/protocol/Messages.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace gs::app {

inline constexpr int kUhidMaxContacts = 16;
inline constexpr std::uint16_t kUhidLogicalMaximum = 32767;
inline constexpr std::size_t kUhidTouchReportSize = 98;

struct UhidPosition {
    std::uint16_t x = 0;
    std::uint16_t y = 0;

    friend bool operator==(const UhidPosition&, const UhidPosition&) = default;
};

using TouchReport = std::array<std::uint8_t, kUhidTouchReportSize>;

[[nodiscard]] UhidPosition naturalPosition(std::uint16_t pictureX, std::uint16_t pictureY, int rotation) noexcept;
[[nodiscard]] std::span<const std::uint8_t> uhidTouchDescriptor() noexcept;
[[nodiscard]] bool sameContacts(const TouchReport& a, const TouchReport& b) noexcept;

class TouchscreenEncoder
{
public:
    using Emit = std::function<bool(const TouchReport& report)>;

    [[nodiscard]] bool encode(const protocol::TouchFrame& frame, bool stale, int rotation, const Emit& emit);
    [[nodiscard]] bool release(const Emit& emit);
    [[nodiscard]] bool holding() const noexcept;
    [[nodiscard]] int ignored() const noexcept { return m_ignored; }
    void reset() noexcept { m_slots = {}; }

private:
    struct Slot {
        bool held = false;
        std::uint8_t pointerId = 0;
        UhidPosition position;
    };

    using Slots = std::array<Slot, kUhidMaxContacts>;

    [[nodiscard]] static TouchReport report(const Slots& slots, std::uint32_t lifting);

    Slots m_slots{};
    int m_ignored = 0;
};

}
