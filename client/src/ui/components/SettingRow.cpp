/**
 * @file SettingRow.cpp
 * @brief SettingRow 实现
 */

#include "SettingRow.h"
#include "ThemeManager.h"
#include "DesignTokens.h"

#include <QVBoxLayout>

namespace Fluent {

SettingRow::SettingRow(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(16, 12, 16, 12);
    m_layout->setSpacing(Spacing::M);

    // 可选图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(24, 24);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setVisible(false);
    m_layout->addWidget(m_iconLabel);

    // 左侧文字区
    auto* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("settingTitle"));
    textLayout->addWidget(m_titleLabel);

    m_descLabel = new QLabel(this);
    m_descLabel->setObjectName(QStringLiteral("settingDesc"));
    m_descLabel->setVisible(false);
    m_descLabel->setWordWrap(true);
    textLayout->addWidget(m_descLabel);

    m_layout->addLayout(textLayout, 1);

    applyStyle();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyStyle();
    });
}

void SettingRow::setTitle(const QString& title)
{
    m_titleLabel->setText(title);
}

void SettingRow::setDescription(const QString& desc)
{
    m_descLabel->setText(desc);
    m_descLabel->setVisible(!desc.isEmpty());
}

void SettingRow::setIcon(const QString& iconText)
{
    m_iconLabel->setText(iconText);
    m_iconLabel->setVisible(!iconText.isEmpty());
}

void SettingRow::setWidget(QWidget* control)
{
    if (m_control) {
        m_layout->removeWidget(m_control);
        m_control->deleteLater();
    }
    m_control = control;
    if (control) {
        m_layout->addWidget(control);
    }
}

void SettingRow::applyStyle()
{
    auto& tm = ThemeManager::instance();

    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 13px; font-weight: 500; color: %1; background: transparent;")
        .arg(tm.textPrimary()));

    m_descLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: transparent;")
        .arg(tm.textTertiary()));

    m_iconLabel->setStyleSheet(QStringLiteral(
        "font-size: 16px; color: %1; background: transparent;")
        .arg(tm.textSecondary()));
}

} // namespace Fluent
