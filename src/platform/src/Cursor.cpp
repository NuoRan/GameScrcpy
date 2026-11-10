#include "gs/platform/Cursor.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace gs::platform {

std::optional<ScreenArea> clientArea(std::uintptr_t window) noexcept
{
    const auto handle = reinterpret_cast<HWND>(window);
    RECT client{};
    if (handle == nullptr || !GetClientRect(handle, &client) || client.right <= client.left ||
        client.bottom <= client.top) {
        return std::nullopt;
    }
    POINT topLeft{client.left, client.top};
    if (!ClientToScreen(handle, &topLeft)) {
        return std::nullopt;
    }
    return ScreenArea{topLeft.x, topLeft.y, client.right - client.left, client.bottom - client.top};
}

std::optional<ScreenPoint> cursorPosition() noexcept
{
    POINT point{};
    if (!GetCursorPos(&point)) {
        return std::nullopt;
    }
    return ScreenPoint{point.x, point.y};
}

std::optional<ScreenArea> pinnedArea(const ScreenArea& client, double x, double y) noexcept
{
    if (client.width <= 0 || client.height <= 0 || !std::isfinite(x) || !std::isfinite(y)) {
        return std::nullopt;
    }
    const auto at = [](int origin, int size, double fraction) {
        const long offset = std::lround(size * std::clamp(fraction, 0.0, 1.0));
        return origin + static_cast<int>(std::min<long>(offset, size - 1));
    };
    return ScreenArea{at(client.x, client.width, x), at(client.y, client.height, y), 1, 1};
}

bool pinCursorInClientArea(std::uintptr_t window, double x, double y) noexcept
{
    const std::optional<ScreenArea> client = clientArea(window);
    const std::optional<ScreenArea> pin = client ? pinnedArea(*client, x, y) : std::nullopt;
    if (!pin) {
        return false;
    }
    const RECT area{pin->x, pin->y, pin->x + pin->width, pin->y + pin->height};
    RECT current{};
    if (GetClipCursor(&current) && current.left == area.left && current.top == area.top &&
        current.right == area.right && current.bottom == area.bottom) {
        return true;
    }
    return ClipCursor(&area) != FALSE;
}

void releaseCursorClip() noexcept
{
    ClipCursor(nullptr);
}

CaptionHover captionHoverOf(const void* message, std::uintptr_t window) noexcept
{
    const auto* msg = static_cast<const MSG*>(message);
    const auto handle = reinterpret_cast<HWND>(window);
    if (msg == nullptr || handle == nullptr || msg->hwnd != handle) {
        return CaptionHover::Unrelated;
    }
    switch (msg->message) {
    case WM_NCMOUSEMOVE:
        if (msg->wParam == HTCAPTION) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, handle, 0};
            TrackMouseEvent(&track);
            return CaptionHover::Over;
        }
        return CaptionHover::Off;
    case WM_NCMOUSELEAVE:
    case WM_MOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
        return CaptionHover::Off;
    default:
        return CaptionHover::Unrelated;
    }
}

bool pointerOnCaption(std::uintptr_t window) noexcept
{
    const auto handle = reinterpret_cast<HWND>(window);
    POINT point{};
    if (handle == nullptr || !GetCursorPos(&point) || GetAncestor(WindowFromPoint(point), GA_ROOT) != handle) {
        return false;
    }
    const LPARAM at = static_cast<LPARAM>((static_cast<std::uint32_t>(point.y & 0xFFFF) << 16) |
                                          static_cast<std::uint32_t>(point.x & 0xFFFF));
    return SendMessageW(handle, WM_NCHITTEST, 0, at) == HTCAPTION;
}

}
