/**
 * @file WinUsbDriverHelper.h
 * @brief Windows WinUSB 驱动安装/卸载辅助
 *
 * 通过 libusb 枚举 Android 设备 VID:PID，
 * 动态生成 WinUSB INF 文件，调用 pnputil 安装/卸载。
 */
#ifndef WINUSB_DRIVER_HELPER_H
#define WINUSB_DRIVER_HELPER_H

#include <QtGlobal>
#ifdef Q_OS_WIN32

#include <QString>
#include <QList>
#include <cstdint>

struct AndroidUsbDevice {
    uint16_t vid = 0;
    uint16_t pid = 0;
    QString  description;   // "Xiaomi (2717:ff40)"
};

class WinUsbDriverHelper
{
public:
    /// 枚举所有已知 Android VID 的 USB 设备
    static QList<AndroidUsbDevice> listAndroidDevices();

    /// 卸载 WinUSB 驱动并恢复系统默认驱动 (需要管理员权限)
    /// 返回 true=成功启动卸载进程
    static bool uninstallWinUsbDriver(QString &errorMsg);

private:
    static bool isAndroidVid(uint16_t vid);
    static QString vidPidString(uint16_t vid, uint16_t pid);
    static QString vendorName(uint16_t vid);
};

#endif // Q_OS_WIN32
#endif // WINUSB_DRIVER_HELPER_H
