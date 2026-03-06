/**
 * @file DeviceCard.h
 * @brief 设备信息卡片 — 展示设备名、连接方式、在线状态、操作按钮
 */
#ifndef DEVICECARD_H
#define DEVICECARD_H

#include "FluentCard.h"
#include "FluentBadge.h"
#include <QLabel>
#include <QPushButton>

namespace Fluent {

class DeviceCard : public FluentCard
{
    Q_OBJECT
public:
    struct DeviceInfo {
        QString serial;
        QString displayName;
        QString connectionType;  // "USB" / "WiFi"
        bool isOnline = true;
        bool isStreaming = false;
    };

    explicit DeviceCard(QWidget* parent = nullptr);

    void setDeviceInfo(const DeviceInfo& info);
    const DeviceInfo& deviceInfo() const { return m_info; }
    QString serial() const { return m_info.serial; }

signals:
    void connectClicked(const QString& serial);
    void disconnectClicked(const QString& serial);
    void detailClicked(const QString& serial);

private:
    void setupUI();
    void updateDisplay();

    DeviceInfo m_info;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_serialLabel = nullptr;
    QLabel* m_typeLabel = nullptr;
    FluentBadge* m_badge = nullptr;
    QPushButton* m_actionBtn = nullptr;
};

} // namespace Fluent
#endif
