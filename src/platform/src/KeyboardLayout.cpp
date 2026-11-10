#include "gs/platform/KeyboardLayout.h"

#include <windows.h>

namespace gs::platform {

std::optional<char32_t> layoutCharacter(std::uint16_t scanCode) noexcept
{
    constexpr UINT kExtended = 0xE000;
    constexpr UINT kMakeCode = 0xFF;
    constexpr UINT kDeadKey = 0x80000000U;
    constexpr char32_t kFirstVisible = 0x21;
    constexpr char32_t kDelete = 0x7F;

    const HKL layout = GetKeyboardLayout(0);
    const UINT code = (scanCode & kExtended) == kExtended ? kExtended | (scanCode & kMakeCode) : scanCode & kMakeCode;
    const UINT virtualKey = MapVirtualKeyExW(code, MAPVK_VSC_TO_VK_EX, layout);
    if (virtualKey == 0) {
        return std::nullopt;
    }
    const auto character = static_cast<char32_t>(MapVirtualKeyExW(virtualKey, MAPVK_VK_TO_CHAR, layout) & ~kDeadKey);
    if (character < kFirstVisible || character == kDelete) {
        return std::nullopt;
    }
    return character;
}

}
