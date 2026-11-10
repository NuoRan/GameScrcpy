#include "gs/platform/TrayNotice.h"

#include <windows.h>

#include <shellapi.h>

#include <cstddef>
#include <utility>

namespace gs::platform {

namespace {

constexpr wchar_t kWindowClass[] = L"GameScrcpyTrayNotice";
constexpr UINT kCallback = WM_APP + 41;
constexpr UINT kIconId = 1;

UINT taskbarCreated()
{
    static const UINT message = RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

void call(const std::function<void()>& callback)
{
    if (callback) {
        const std::function<void()> kept = callback;
        kept();
    }
}

}

struct TrayNotice::Impl {
    std::function<void()> clicked;
    std::function<void()> activated;
    std::vector<TrayMenuItem> menu;
    HWND window = nullptr;
    bool added = false;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (impl != nullptr && message == kCallback) {
            impl->handle(LOWORD(lParam), wParam);
            return 0;
        }
        if (const UINT created = taskbarCreated(); impl != nullptr && created != 0 && message == created && impl->added) {
            impl->added = false;
            impl->add();
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void handle(UINT event, WPARAM anchor)
    {
        if (event == NIN_BALLOONUSERCLICK) {
            call(clicked);
        } else if (event == WM_LBUTTONUP) {
            call(activated ? activated : clicked);
        } else if (event == WM_CONTEXTMENU) {
            showMenu(static_cast<short>(LOWORD(anchor)), static_cast<short>(HIWORD(anchor)));
        }
    }

    void showMenu(int x, int y)
    {
        if (menu.empty() || window == nullptr) {
            return;
        }
        HMENU popup = CreatePopupMenu();
        if (popup == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < menu.size(); ++index) {
            AppendMenuW(popup, MF_STRING, static_cast<UINT_PTR>(index + 1), menu[index].text.c_str());
        }
        SetForegroundWindow(window);
        const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        const auto chosen = static_cast<std::size_t>(TrackPopupMenuEx(
            popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, x, y, window, nullptr));
        PostMessageW(window, WM_NULL, 0, 0);
        DestroyMenu(popup);
        if (chosen >= 1 && chosen <= menu.size()) {
            call(menu[chosen - 1].chosen);
        }
    }

    [[nodiscard]] NOTIFYICONDATAW icon() const
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = kIconId;
        return data;
    }

    bool add()
    {
        NOTIFYICONDATAW data = icon();
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = kCallback;
        data.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), L"IDI_ICON1", IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                   LR_DEFAULTCOLOR | LR_SHARED));
        if (data.hIcon == nullptr) {
            data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        wcsncpy_s(data.szTip, L"GameScrcpy", _TRUNCATE);
        if (Shell_NotifyIconW(NIM_ADD, &data) == FALSE) {
            return false;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
        added = true;
        return true;
    }
};

TrayNotice::TrayNotice(std::function<void()> clicked) : m_impl(std::make_unique<Impl>())
{
    m_impl->clicked = std::move(clicked);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &Impl::windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return;
    }
    m_impl->window =
        CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, m_impl.get());
}

TrayNotice::~TrayNotice()
{
    if (m_impl->added) {
        NOTIFYICONDATAW data = m_impl->icon();
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
    if (m_impl->window != nullptr) {
        DestroyWindow(m_impl->window);
    }
}

bool TrayNotice::show(const std::wstring& title, const std::wstring& text)
{
    if (!showIcon()) {
        return false;
    }
    NOTIFYICONDATAW data = m_impl->icon();
    data.uFlags = NIF_INFO;
    wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, text.empty() ? L" " : text.c_str(), _TRUNCATE);
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
}

bool TrayNotice::showIcon()
{
    return m_impl->window != nullptr && (m_impl->added || m_impl->add());
}

void TrayNotice::setActivated(std::function<void()> activated)
{
    m_impl->activated = std::move(activated);
}

void TrayNotice::setMenu(std::vector<TrayMenuItem> items)
{
    m_impl->menu = std::move(items);
}

}
