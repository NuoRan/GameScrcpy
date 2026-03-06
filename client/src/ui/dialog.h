/**
 * @file dialog.h
 * @brief 应用主对话框 / Application Main Dialog
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 主界面功能 / Main interface features:
 * - 设备列表管理 / Device list management
 * - USB/WiFi 连接控制 / USB/WiFi connection control
 * - 系统托盘 / System tray
 * - 日志显示 / Log display
 */

#ifndef DIALOG_H
#define DIALOG_H

#include <QWidget>
#include <QPointer>
#include <QMessageBox>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QListWidget>
#include <QTimer>
#include <QMap>
#include <QEvent>
#include <QStringList>
#include <QLabel>
#include <QProgressBar>
#include <QFrame>

#include "adbprocess.h"
#include "GameScrcpyCore.h"

namespace Ui
{
    class Widget;
}

class QYUVOpenGLWidget;
class SettingsDialog;
class TerminalDialog;
class VideoForm;

/**
 * @brief 应用主对话框 / Application Main Dialog
 *
 * 极简现代风格的主界面，功能 / Minimalist modern-style main interface:
 * - 设备扫描与连接 / Device scanning and connection
 * - 连接参数配置 / Connection parameter configuration
 * - 系统托盘集成 / System tray integration
 * - 日志输出 / Log output
 */
class Dialog : public QWidget
{
    Q_OBJECT

public:
    explicit Dialog(QWidget *parent = 0);
    ~Dialog();

    void outLog(const QString &log, bool newLine = true);
    bool filterLog(const QString &log);
    void getIPbyIp();

private slots:
    // 设备连接状态回调
    void onDeviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void onDeviceDisconnected(QString serial);

    // 主界面按钮
    void on_updateDevice_clicked();
    void on_usbConnectBtn_clicked();
    void on_wifiConnectBtn_clicked();
    void on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item);
    void on_autoUpdatecheckBox_toggled(bool checked);

    // 底部工具栏按钮
    void on_settingsBtn_clicked();
    void on_terminalBtn_clicked();
    void on_langBtn_clicked();

    // 设置对话框信号处理
    void onStartServer();
    void onStopServer();
    void onStopAllServers();
    void onWirelessConnect();
    void onWirelessDisconnect();
    void onGetDeviceIP();
    void onStartAdbd();

    // 终端对话框信号处理
    void onExecuteCommand(const QString &cmd);
    void onStopCommand();

private:
    enum class WifiConnectMode {
        None,
        Manual,
        AutoReconnect,
    };

    bool checkAdbRun();
    bool isWifiSerial(const QString &serial) const;
    QString extractSerialFromItemText(const QString &text) const;
    void showStatusDialog(const QString &title, const QString &message, QMessageBox::Icon icon = QMessageBox::Information);
    void requestDeviceRefresh(bool force = false);
    void tryStartServerForSerial(const QString &serial);
    QString findWifiSerialByAddr(const QString &addr, const QStringList &devices) const;
    void startAutoReconnectFromHistory();
    void tryNextAutoReconnect();
    void updateStatusBar(const QString &text, bool showProgress = true);
    void hideStatusBar();
    void initUI();
    void applyModernStyle();
    void retranslateUi();
    void updateLangBtnText();
    void updateBootConfig(bool toView = true);
    void execAdbCmd();
    void delayMs(int ms);
    void slotActivated(QSystemTrayIcon::ActivationReason reason);
    int findDeviceFromeSerialBox(bool wifi);
    quint32 getBitRate();
    const QString &getServerPath();

    // 历史记录
    void loadIpHistory();
    void saveIpHistory(const QString &ip);
    void loadPortHistory();
    void savePortHistory(const QString &port);

    // 同步设置对话框数据
    void syncSettingsToDialog();

protected:
    void changeEvent(QEvent *event) override;

private:
    Ui::Widget *ui;
    SettingsDialog *m_settingsDialog;
    TerminalDialog *m_terminalDialog;
    qsc::AdbProcess m_adb;
    QSystemTrayIcon *m_hideIcon;
    QMenu *m_menu;
    QAction *m_showWindow;
    QAction *m_quit;
    QTimer m_autoUpdatetimer;
    QTimer m_deviceWatchTimer;
    QString m_currentSerial;  // 当前选中的设备序列号
    QMap<QString, VideoForm*> m_videoForms;  // serial -> VideoForm 映射
    QStringList m_lastDeviceSerials;

    bool m_pendingUsbConnect = false;
    bool m_hasInitialDeviceScan = false;

    WifiConnectMode m_wifiConnectMode = WifiConnectMode::None;
    QString m_currentWifiAddr;
    bool m_pendingManualWifiStart = false;
    int m_pendingManualWifiRefreshRetry = 0;
    QStringList m_autoReconnectQueue;
    int m_autoReconnectIndex = 0;
    bool m_hasTriedAutoReconnect = false;

    // 底部状态条
    QFrame *m_statusBarFrame = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_statusProgress = nullptr;
    QTimer m_statusHideTimer;

    // 设备管理监听器 ID
    int m_deviceConnectedListenerId = 0;
    int m_deviceDisconnectedListenerId = 0;
};

#endif // DIALOG_H
