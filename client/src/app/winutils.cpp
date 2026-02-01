#include <QDebug>
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi")

#include "winutils.h"

// DWM 属性常量定义
enum : WORD
{
    DwmwaUseImmersiveDarkMode = 20,
    DwmwaUseImmersiveDarkModeBefore20h1 = 19
};

WinUtils::WinUtils(){};

WinUtils::~WinUtils(){};

// ---------------------------------------------------------
// 设置窗口边框颜色（暗色模式支持）
// 调用 DwmSetWindowAttribute 修改窗口属性
// ---------------------------------------------------------
bool WinUtils::setDarkBorderToWindow(const HWND &hwnd, const bool &d)
{
    const BOOL darkBorder = d ? TRUE : FALSE;
    // 尝试兼容不同版本的 Windows 10/11
    const bool ok = SUCCEEDED(DwmSetWindowAttribute(hwnd, DwmwaUseImmersiveDarkMode, &darkBorder, sizeof(darkBorder)))
                    || SUCCEEDED(DwmSetWindowAttribute(hwnd, DwmwaUseImmersiveDarkModeBefore20h1, &darkBorder, sizeof(darkBorder)));
    if (!ok)
        qWarning("%s: Unable to set dark window border.", __FUNCTION__);
    return ok;
}
