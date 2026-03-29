#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>
#include "FluentComboBox.h"
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QEvent>

namespace Fluent { class FluentCard; class FluentToggle; class FluentButton; class SettingRow; }

/**
 * @brief 设置页 — 替代旧 SettingsDialog
 *
 * 分四个 Section:
 *   1. 视频参数 (码率 / 帧率 / 分辨率 / 编码)
 *   2. 显示选项 (反向连接 / 工具栏 / 无边框 / FPS)
 *   3. 外观 (主题 / 强调色)
 *   4. 无线连接 (IP / 端口 / 连接 / 断开 / 获取IP / ADBD)
 */
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    // 给 MainWindow 读取
    quint32  getBitRate() const;
    quint16  getMaxSize() const;
    int      getMaxFps() const;
    QString  getVideoCodecName() const;
    int      getVideoCodecIndex() const;
    bool     isReverseConnect() const;
    bool     showToolbar() const;
    bool     isFrameless() const;
    bool     showFPS() const;

    // WiFi 地址
    QString  getDeviceIP() const;
    QString  getDevicePort() const;
    void     setDeviceIP(const QString &ip);

    // 触控模式 (0=Scrcpy, 1=AOA HID, 2=ESP32)
    int      getTouchMode() const;

    // 初始化时从 Config 同步值
    void     syncFromConfig();

    // 将当前 UI 状态保存到 Config（持久化）
    void     saveToConfig();

signals:
    void wirelessConnect(const QString &address);
    void wirelessDisconnect();
    void requestDeviceIP();
    void startAdbd();
    void restartOnboarding();
    void touchModeChanged(int mode);

protected:
    void changeEvent(QEvent *event) override;

private:
    void setupUI();
    void retranslateUi();

    // 视频
    QLabel    *m_videoTitle   = nullptr;
    QLabel    *m_bitrateLabel = nullptr;
    QLabel    *m_fpsLabel     = nullptr;
    QLabel    *m_sizeLabel    = nullptr;
    QLabel    *m_codecLabel   = nullptr;
    QLineEdit *m_bitRateEdit  = nullptr;
    QLabel    *m_bitRateUnitLabel = nullptr;
    Fluent::FluentComboBox *m_fpsBox       = nullptr;
    Fluent::FluentComboBox *m_maxSizeBox   = nullptr;
    Fluent::FluentComboBox *m_codecBox     = nullptr;

    // 显示选项
    QLabel    *m_optionsTitle    = nullptr;
    Fluent::FluentToggle *m_reverseToggle  = nullptr;
    Fluent::FluentToggle *m_toolbarToggle  = nullptr;
    Fluent::FluentToggle *m_framelessToggle = nullptr;
    Fluent::FluentToggle *m_fpsToggle      = nullptr;

    // 通道控制
    QLabel    *m_channelTitle   = nullptr;
    Fluent::FluentToggle *m_videoChannelToggle   = nullptr;
    Fluent::FluentToggle *m_audioChannelToggle   = nullptr;
    Fluent::FluentToggle *m_controlChannelToggle = nullptr;
    Fluent::FluentToggle *m_auxChannelToggle     = nullptr;

    // 触控设置
    QLabel    *m_touchTitle     = nullptr;
    Fluent::FluentComboBox *m_touchModeBox   = nullptr;
    QLineEdit *m_esp32PortEdit  = nullptr;
    Fluent::FluentComboBox *m_aoaResBox      = nullptr;

    // 外观
    QLabel    *m_appearTitle = nullptr;
    Fluent::FluentComboBox *m_themeBox    = nullptr;
    Fluent::FluentComboBox *m_accentBox   = nullptr;

    // WiFi
    QLabel    *m_wifiTitle   = nullptr;
    QLabel    *m_ipLabel     = nullptr;
    Fluent::FluentComboBox *m_ipEdit      = nullptr;
    Fluent::FluentComboBox *m_portEdit    = nullptr;
    Fluent::FluentButton *m_connectBtn    = nullptr;
    Fluent::FluentButton *m_disconnectBtn = nullptr;
    QPushButton *m_getIpBtn  = nullptr;
    QPushButton *m_adbdBtn   = nullptr;

    // 其他
    QLabel      *m_otherTitle     = nullptr;
    Fluent::FluentButton *m_onboardingBtn = nullptr;

};

#endif // SETTINGSPAGE_H
