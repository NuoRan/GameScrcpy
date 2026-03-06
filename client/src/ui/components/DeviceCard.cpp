/**
 * @file DeviceCard.cpp
 */
#include "DeviceCard.h"
#include "ThemeManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace Fluent {

DeviceCard::DeviceCard(QWidget* parent) : FluentCard(parent)
{
    setupUI();
    setClickable(true);
    connect(this, &FluentCard::clicked, this, [this]() {
        if (m_info.isStreaming)
            emit detailClicked(m_info.serial);
        else
            emit connectClicked(m_info.serial);
    });
}

void DeviceCard::setupUI()
{
    auto& tm = ThemeManager::instance();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(12);

    // 设备图标
    auto* icon = new QLabel(QStringLiteral("\xF0\x9F\x93\xB1")); // 📱
    icon->setFixedSize(36, 36);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral(
        "font-size: 24px; background: transparent;"));
    layout->addWidget(icon);

    // 文字区
    auto* textLayout = new QVBoxLayout;
    textLayout->setSpacing(2);

    m_nameLabel = new QLabel;
    m_nameLabel->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-weight: 600; color: %1; background: transparent;").arg(tm.textPrimary()));

    m_serialLabel = new QLabel;
    m_serialLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: transparent;").arg(tm.textTertiary()));

    textLayout->addWidget(m_nameLabel);
    textLayout->addWidget(m_serialLabel);
    layout->addLayout(textLayout, 1);

    // 连接方式标签
    m_typeLabel = new QLabel;
    m_typeLabel->setFixedWidth(40);
    m_typeLabel->setAlignment(Qt::AlignCenter);
    m_typeLabel->setStyleSheet(QStringLiteral(
        "font-size: 10px; font-weight: 600; color: %1; background: %2; "
        "border-radius: 4px; padding: 2px 6px;")
        .arg(tm.textPrimary(), tm.surface()));
    layout->addWidget(m_typeLabel);

    // 状态徽标
    m_badge = new FluentBadge;
    layout->addWidget(m_badge);

    // 操作按钮
    m_actionBtn = new QPushButton;
    m_actionBtn->setFixedSize(36, 36);
    m_actionBtn->setCursor(Qt::PointingHandCursor);
    m_actionBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: none; border-radius: 8px; font-size: 16px; }"
        "QPushButton:hover { background: %2; }")
        .arg(tm.accentPrimary(), tm.accentHover()));
    layout->addWidget(m_actionBtn);

    connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
        if (m_info.isStreaming)
            emit disconnectClicked(m_info.serial);
        else
            emit connectClicked(m_info.serial);
    });
}

void DeviceCard::setDeviceInfo(const DeviceInfo& info)
{
    m_info = info;
    updateDisplay();
}

void DeviceCard::updateDisplay()
{
    m_nameLabel->setText(m_info.displayName.isEmpty() ? m_info.serial : m_info.displayName);
    m_serialLabel->setText(m_info.serial);
    m_typeLabel->setText(m_info.connectionType);

    if (m_info.isStreaming) {
        m_badge->setStatus(FluentBadge::Streaming);
        m_actionBtn->setText(QStringLiteral("\xE2\x96\xA0")); // ■
    } else if (m_info.isOnline) {
        m_badge->setStatus(FluentBadge::Online);
        m_actionBtn->setText(QStringLiteral("\xE2\x96\xB6")); // ▶
    } else {
        m_badge->setStatus(FluentBadge::Offline);
        m_actionBtn->setText(QStringLiteral("\xE2\x96\xB6"));
    }
}

} // namespace Fluent
