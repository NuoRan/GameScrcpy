/**
 * @file WinUsbDriverHelper.cpp
 * @brief WinUSB 驱动安装/卸载实现
 */
#include "WinUsbDriverHelper.h"

#ifdef Q_OS_WIN32

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QProcess>

#include <Windows.h>
#include <shellapi.h>

// libusb
#include <libusb.h>

bool WinUsbDriverHelper::isAndroidVid(uint16_t vid)
{
    switch (vid) {
    case 0x18D1: // Google
    case 0x2717: // Xiaomi
    case 0x04E8: // Samsung
    case 0x22B8: // Motorola
    case 0x0BB4: // HTC
    case 0x12D1: // Huawei
    case 0x2A70: // OnePlus
    case 0x1004: // LG
    case 0x0FCE: // Sony
    case 0x2916: // Yota
    case 0x1949: // Honor
    case 0x19D2: // ZTE
    case 0x2C7C: // Quectel
    case 0x1782: // Spreadtrum
    case 0x05C6: // Qualcomm
    case 0x2A45: // Meizu
    case 0x0E8D: // MediaTek
    case 0x1EBF: // vivo
    case 0x2B4C: // Realme
        return true;
    default:
        return false;
    }
}

QString WinUsbDriverHelper::vendorName(uint16_t vid)
{
    switch (vid) {
    case 0x18D1: return "Google";
    case 0x2717: return "Xiaomi";
    case 0x04E8: return "Samsung";
    case 0x22B8: return "Motorola";
    case 0x0BB4: return "HTC";
    case 0x12D1: return "Huawei";
    case 0x2A70: return "OnePlus";
    case 0x1004: return "LG";
    case 0x0FCE: return "Sony";
    case 0x2916: return "Yota";
    case 0x1949: return "Honor";
    case 0x19D2: return "ZTE";
    case 0x2A45: return "Meizu";
    case 0x0E8D: return "MediaTek";
    case 0x1EBF: return "vivo";
    case 0x2B4C: return "Realme";
    default:     return "Android";
    }
}

QString WinUsbDriverHelper::vidPidString(uint16_t vid, uint16_t pid)
{
    return QString("%1:%2")
        .arg(vid, 4, 16, QChar('0'))
        .arg(pid, 4, 16, QChar('0'));
}

QList<AndroidUsbDevice> WinUsbDriverHelper::listAndroidDevices()
{
    QList<AndroidUsbDevice> result;

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) < 0) return result;

    libusb_device **devList = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &devList);

    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devList[i], &desc) != 0) continue;
        if (!isAndroidVid(desc.idVendor)) continue;

        AndroidUsbDevice dev;
        dev.vid = desc.idVendor;
        dev.pid = desc.idProduct;
        dev.description = QString("%1 (%2)")
            .arg(vendorName(desc.idVendor), vidPidString(desc.idVendor, desc.idProduct));
        result.append(dev);
    }

    libusb_free_device_list(devList, 1);
    libusb_exit(ctx);
    return result;
}

bool WinUsbDriverHelper::uninstallWinUsbDriver(QString &errorMsg)
{
    // 策略：删除我们安装的驱动，然后扫描硬件让系统恢复默认驱动
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                      + "/GameScrcpy_WinUSB";

    // 写一个临时 bat 脚本然后提权运行
    QString batPath = tempDir + "/restore_adb.bat";
    QDir().mkpath(tempDir);
    QFile batFile(batPath);
    if (batFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&batFile);
        ts << "@echo off\r\n";
        ts << "chcp 65001 >nul 2>&1\r\n";
        ts << "echo.\r\n";
        ts << "echo  ==== GameScrcpy: 恢复 ADB 驱动 ====\r\n";
        ts << "echo.\r\n";
        ts << "echo  正在搜索并删除 WinUSB 驱动...\r\n";
        // 枚举所有 OEM drivers，找到包含 GameScrcpy 的并删除
        ts << "setlocal enabledelayedexpansion\r\n";
        ts << "set FOUND=0\r\n";
        ts << "for /f \"tokens=*\" %%L in ('pnputil /enum-drivers') do (\r\n";
        ts << "    echo %%L | findstr /B /C:\"oem\" >nul 2>&1 && set OEMDRV=%%L\r\n";
        ts << "    echo %%L | findstr /i /C:\"GameScrcpy\" >nul 2>&1 && (\r\n";
        ts << "        for /f \"tokens=1\" %%D in (\"!OEMDRV!\") do (\r\n";
        ts << "            echo  删除: %%D\r\n";
        ts << "            pnputil /delete-driver %%D /uninstall /force\r\n";
        ts << "            set FOUND=1\r\n";
        ts << "        )\r\n";
        ts << "    )\r\n";
        ts << ")\r\n";
        ts << "if !FOUND!==0 (\r\n";
        ts << "    echo  未找到 GameScrcpy 安装的驱动.\r\n";
        ts << "    echo  尝试扫描硬件恢复默认驱动...\r\n";
        ts << ")\r\n";
        ts << "echo.\r\n";
        ts << "echo  正在扫描硬件变更...\r\n";
        ts << "pnputil /scan-devices\r\n";
        ts << "echo.\r\n";
        ts << "echo  ==== 完成! 请拔插 USB 线缆 ====\r\n";
        ts << "echo.\r\n";
        ts << "timeout /t 5\r\n";
        batFile.close();
    }

    QString batArgs = QString("/c \"%1\"").arg(QDir::toNativeSeparators(batPath));

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    std::wstring wArgs = batArgs.toStdWString();
    sei.lpParameters = wArgs.c_str();
    sei.nShow  = SW_SHOW;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            errorMsg = "用户取消了管理员权限请求";
        } else {
            errorMsg = QString("启动恢复脚本失败，错误码: %1").arg(err);
        }
        return false;
    }

    return true;
}

#endif // Q_OS_WIN32
