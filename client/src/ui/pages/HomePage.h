#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QEvent>

namespace Fluent { class FluentCard; class FluentButton; }

/**
 * @brief 首页 — 设备列表 + 快速连接
 */
class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);

    void updateDeviceList(const QStringList &serials);
    void setDeviceIP(const QString &ip);

    /// 触控方式变化时调用, 动态显示/隐藏直连条目
    void onTouchMethodChanged(int method);

signals:
    void requestUsbConnect();
    void requestWifiConnect(const QString &address);
    void requestRefresh();
    void requestDeviceConnect(const QString &serial);
    void requestDirectConnect();  // AOA/ESP32 直连

protected:
    void changeEvent(QEvent *event) override;

private:
    void setupUI();
    void retranslateUi();
    void updateDirectConnectItem();

    QLabel       *m_titleLabel   = nullptr;
    QLabel       *m_deviceTitle  = nullptr;
    QLabel       *m_deviceHint   = nullptr;

    Fluent::FluentButton *m_usbBtn  = nullptr;
    Fluent::FluentButton *m_wifiBtn = nullptr;
    QPushButton  *m_refreshBtn     = nullptr;
    QCheckBox    *m_autoRefreshChk = nullptr;
    QListWidget  *m_deviceList     = nullptr;

    QString       m_lastDeviceIP;
    int           m_touchMethod = 0;  // 当前触控方式 (TouchMethod 枚举值)
    QStringList   m_lastSerials;      // 缓存最近的 ADB 设备列表
};

#endif // HOMEPAGE_H
