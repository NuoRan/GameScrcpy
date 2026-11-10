#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace gs::platform {

[[nodiscard]] bool isInFront(std::uintptr_t window) noexcept;

namespace mouse_button {
inline constexpr std::uint8_t kLeft = 0x01;
inline constexpr std::uint8_t kRight = 0x02;
inline constexpr std::uint8_t kMiddle = 0x04;
inline constexpr std::uint8_t kX1 = 0x08;
inline constexpr std::uint8_t kX2 = 0x10;
}

[[nodiscard]] std::uint8_t pressedMouseButtons() noexcept;
[[nodiscard]] bool scanCodeHeld(std::uint16_t scanCode) noexcept;

class ForegroundWatcher
{
public:
    using Changed = std::function<void()>;

    explicit ForegroundWatcher(Changed changed);
    ~ForegroundWatcher();
    ForegroundWatcher(const ForegroundWatcher&) = delete;
    ForegroundWatcher& operator=(const ForegroundWatcher&) = delete;

    [[nodiscard]] bool started() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
