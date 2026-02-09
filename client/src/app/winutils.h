#ifndef WINUTILS_H
#define WINUTILS_H

#include <QApplication>
#include <Windows.h>

// ---------------------------------------------------------
// Windows 平台工具类 / Windows Platform Utility Class
// 提供 Windows API 的封装调用 / Provides wrappers for Windows API calls
// ---------------------------------------------------------
class WinUtils
{
public:
    WinUtils();
    ~WinUtils();

    // 设置窗口是否使用深色边框 / Set whether window uses dark border
    static bool setDarkBorderToWindow(const HWND &hwnd, const bool &d);
};

#endif // WINUTILS_H
