/**
 * @file MainWindow.h
 * @brief Fluent Focus 主窗口 — NavigationView + QStackedWidget
 *
 * 方案 A 主窗口，替代旧的 Dialog：
 * - 左侧 NavigationView (首页/设置/终端)
 * - 右侧 QStackedWidget 页面切换
 * - 系统托盘集成
 * - 设备事件路由 (DeviceSession 信号 → 页面更新)
 *
 * 注意: 不包含性能监控页面 (已移除)
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QMap>
#include <QPointer>

#include "adbprocess.h"
#include "GameScrcpyCore.h"

namespace Fluent { class NavigationView; class OnboardingOverlay; }

class VideoForm;
class HomePage;
class SettingsPage;
class TerminalPage;
class HelpDialog;

/**
 * @brief 方案 A 主窗口 — NavigationView + 页面容器
 */
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// 日志输出 (兼容旧接口)
    void outLog(const QString& log, bool newLine = true);
    bool filterLog(const QString& log);

signals:
    void deviceListUpdated(const QStringList& serials);

private slots:
    // 导航
    void onNavItemClicked(const QString& id);

    // 设备连接状态回调
    void onDeviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void onDeviceDisconnected(const QString& serial);

    // 设备操作 (来自 HomePage)
    void onRequestUsbConnect();
    void onRequestWifiConnect(const QString& address);
    void onRequestRefresh();
    void onRequestDeviceConnect(const QString& serial);
    void onRequestDirectConnect();
    void onRequestWifiDisconnect();
    void onRequestGetDeviceIP();
    void onRequestStartAdbd();

    // 终端
    void onExecuteCommand(const QString& cmd);
    void onStopCommand();

private:
    // 初始化
    void setupUI();
    void setupNavigation();
    void setupPages();
    void setupTray();
    void setupDeviceWatcher();
    void connectSignals();

    // 样式
    void applyStyle();

    // 设备管理
    bool checkAdbRun();
    void requestDeviceRefresh(bool force = false);
    void tryStartServerForSerial(const QString& serial);
    bool isWifiSerial(const QString& serial) const;
    QString extractSerialFromItemText(const QString& text) const;
    QString findWifiSerialByAddr(const QString& addr, const QStringList& devices) const;

    // WiFi 自动重连
    void startAutoReconnectFromHistory();
    void tryNextAutoReconnect();

    // 工具
    void execAdbCmd();
    quint32 getBitRate();
    const QString& getServerPath();

    // i18n
    void retranslateUi();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    // 导航
    Fluent::NavigationView* m_navView = nullptr;
    QStackedWidget* m_stack = nullptr;

    // 页面
    HomePage*     m_homePage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    TerminalPage* m_terminalPage = nullptr;


    // ADB
    qsc::AdbProcess m_adb;

    // 设备
    QMap<QString, VideoForm*> m_videoForms;
    QStringList m_lastDeviceSerials;
    QTimer m_deviceWatchTimer;
    QTimer m_autoUpdateTimer;
    QString m_currentSerial;

    // WiFi
    enum class WifiConnectMode { None, Manual, AutoReconnect };
    WifiConnectMode m_wifiConnectMode = WifiConnectMode::None;
    QString m_currentWifiAddr;
    bool m_pendingManualWifiStart = false;
    int m_pendingManualWifiRefreshRetry = 0;
    QStringList m_autoReconnectQueue;
    int m_autoReconnectIndex = 0;
    bool m_hasTriedAutoReconnect = false;

    bool m_pendingUsbConnect = false;
    bool m_hasInitialDeviceScan = false;

    // USB → WiFi 切换流程
    qsc::AdbProcess m_wifiSetupAdb;
    enum class WifiSetupStep { None, TcpipSent, GettingIP, GettingIPFallback, Connecting };
    WifiSetupStep m_wifiSetupStep = WifiSetupStep::None;
    QString m_wifiSetupSerial;  // 正在操作的 USB 设备序列号
    void startUsbToWifiFlow(const QString& usbSerial);
    void onWifiSetupResult(qsc::AdbProcess::ADB_EXEC_RESULT result);

    // 引导系统
    Fluent::OnboardingOverlay* m_onboarding = nullptr;
    void startOnboarding();

    // 帮助对话框
    HelpDialog* m_helpDialog = nullptr;

    // 设备管理监听器 ID
    int m_deviceConnectedListenerId = 0;
    int m_deviceDisconnectedListenerId = 0;
};

#endif // MAINWINDOW_H
