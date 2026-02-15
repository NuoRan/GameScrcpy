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

    /**
     * MMCSS 实时线程调度 / MMCSS Real-time Thread Scheduling
     * @param taskName MMCSS 任务类别: "Pro Audio"(解码) / "Playback"(渲染) / "Games"(控制)
     * @return MMCSS 任务句柄，失败返回 nullptr
     */
    static void* enableMMCSS(const char* taskName);

    /**
     * 撤销 MMCSS 线程调度提升
     * @param taskHandle enableMMCSS 返回的句柄
     */
    static void disableMMCSS(void* taskHandle);
};

#endif // WINUTILS_H
