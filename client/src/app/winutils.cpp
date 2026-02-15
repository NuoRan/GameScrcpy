#include <QDebug>
#include <Windows.h>
#include <dwmapi.h>
#include <avrt.h>
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "avrt")

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

// MMCSS 实时线程调度
void* WinUtils::enableMMCSS(const char* taskName)
{
    DWORD taskIndex = 0;
    // AvSetMmThreadCharacteristicsA 将当前线程注册到 MMCSS 任务类别
    // 内核会自动提升线程调度优先级，保证 CPU 时间片
    HANDLE hTask = AvSetMmThreadCharacteristicsA(taskName, &taskIndex);
    if (hTask) {
        // 进一步将 MMCSS 优先级设为 CRITICAL（最高级别）
        if (!AvSetMmThreadPriority(hTask, AVRT_PRIORITY_CRITICAL)) {
            qWarning("[MMCSS] Failed to set thread priority to CRITICAL for task '%s'", taskName);
        }
        qInfo("[MMCSS] Thread registered: task='%s', index=%u", taskName, taskIndex);
    } else {
        DWORD err = GetLastError();
        qWarning("[MMCSS] AvSetMmThreadCharacteristics failed for '%s': error=%lu", taskName, err);
    }
    return static_cast<void*>(hTask);
}

void WinUtils::disableMMCSS(void* taskHandle)
{
    if (taskHandle) {
        HANDLE hTask = static_cast<HANDLE>(taskHandle);
        if (AvRevertMmThreadCharacteristics(hTask)) {
            qInfo("[MMCSS] Thread unregistered successfully");
        } else {
            qWarning("[MMCSS] AvRevertMmThreadCharacteristics failed");
        }
    }
}
