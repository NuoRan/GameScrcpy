#include "gs/platform/Foreground.h"

#include <windows.h>

#include <map>
#include <utility>

namespace gs::platform {

bool isInFront(std::uintptr_t window) noexcept
{
    const auto target = reinterpret_cast<HWND>(window);
    const HWND front = GetForegroundWindow();
    return target != nullptr && front != nullptr && (front == target || GetAncestor(front, GA_ROOTOWNER) == target);
}

std::uint8_t pressedMouseButtons() noexcept
{
    constexpr std::pair<int, std::uint8_t> kButtons[] = {{VK_LBUTTON, mouse_button::kLeft},
                                                         {VK_RBUTTON, mouse_button::kRight},
                                                         {VK_MBUTTON, mouse_button::kMiddle},
                                                         {VK_XBUTTON1, mouse_button::kX1},
                                                         {VK_XBUTTON2, mouse_button::kX2}};
    std::uint8_t pressed = 0;
    for (const auto& [key, bit] : kButtons) {
        if ((GetAsyncKeyState(key) & 0x8000) != 0) {
            pressed = static_cast<std::uint8_t>(pressed | bit);
        }
    }
    return pressed;
}

bool scanCodeHeld(std::uint16_t scanCode) noexcept
{
    const UINT virtualKey = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
    return virtualKey != 0 && (GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0;
}

struct ForegroundWatcher::Impl {
    Changed changed;
    HWINEVENTHOOK hook = nullptr;
    HWINEVENTHOOK desktopHook = nullptr;

    static std::map<HWINEVENTHOOK, Impl*>& registry()
    {
        thread_local std::map<HWINEVENTHOOK, Impl*> watchers;
        return watchers;
    }

    static void CALLBACK onEvent(HWINEVENTHOOK source, DWORD, HWND, LONG, LONG, DWORD, DWORD)
    {
        const std::map<HWINEVENTHOOK, Impl*>& watchers = registry();
        if (const auto found = watchers.find(source); found != watchers.end() && found->second->changed) {
            found->second->changed();
        }
    }
};

ForegroundWatcher::ForegroundWatcher(Changed changed)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->changed = std::move(changed);
    m_impl->hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &Impl::onEvent, 0, 0,
                                   WINEVENT_OUTOFCONTEXT);
    if (m_impl->hook != nullptr) {
        Impl::registry().emplace(m_impl->hook, m_impl.get());
    }
    m_impl->desktopHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH, nullptr,
                                          &Impl::onEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (m_impl->desktopHook != nullptr) {
        Impl::registry().emplace(m_impl->desktopHook, m_impl.get());
    }
}

ForegroundWatcher::~ForegroundWatcher()
{
    for (HWINEVENTHOOK hook : {m_impl->hook, m_impl->desktopHook}) {
        if (hook != nullptr) {
            Impl::registry().erase(hook);
            UnhookWinEvent(hook);
        }
    }
}

bool ForegroundWatcher::started() const noexcept
{
    return m_impl->hook != nullptr;
}

}
