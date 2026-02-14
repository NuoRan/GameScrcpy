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
     * [超低延迟优化] MMCSS 实时线程调度 / MMCSS Real-time Thread Scheduling
     *
     * 使用 Windows Multimedia Class Scheduler Service (MMCSS) 提升线程优先级。
     * MMCSS 比 SetThreadPriority 更有效：
     * - 提供内核级优先级提升
     * - 保证 CPU 时间片分配
     * - 被 Windows 音视频管线广泛使用
     *
     * @param taskName MMCSS 任务类别名称:
     *   - "Pro Audio": 最高优先级，用于解码线程
     *   - "Playback": 高优先级，用于渲染线程
     *   - "Games": 游戏优先级，用于控制线程
     * @return MMCSS 任务句柄，用于后续 revert。失败返回 nullptr
     */
    static void* enableMMCSS(const char* taskName);

    /**
     * 撤销 MMCSS 线程调度提升
     * @param taskHandle enableMMCSS 返回的句柄
     */
    static void disableMMCSS(void* taskHandle);
};

#endif // WINUTILS_H
