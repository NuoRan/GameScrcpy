#pragma once

#include <cstdint>
#include <optional>

namespace gs::platform {

struct ScreenArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ScreenPoint {
    int x = 0;
    int y = 0;
};

struct AreaShare {
    double left = 0.0;
    double top = 0.0;
    double width = 1.0;
    double height = 1.0;
};

enum class CaptionHover : std::uint8_t { Unrelated, Over, Off };

[[nodiscard]] std::optional<ScreenArea> clientArea(std::uintptr_t window) noexcept;
[[nodiscard]] std::optional<ScreenPoint> cursorPosition() noexcept;
[[nodiscard]] std::optional<ScreenArea> pinnedArea(const ScreenArea& client, double x, double y) noexcept;
[[nodiscard]] bool pinCursorInClientArea(std::uintptr_t window, double x, double y) noexcept;
void releaseCursorClip() noexcept;
[[nodiscard]] CaptionHover captionHoverOf(const void* message, std::uintptr_t window) noexcept;
[[nodiscard]] bool pointerOnCaption(std::uintptr_t window) noexcept;

}
