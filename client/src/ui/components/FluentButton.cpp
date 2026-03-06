/**
 * @file FluentButton.cpp
 * @brief FluentButton 实现
 */

#include "FluentButton.h"
#include "ThemeManager.h"
#include "DesignTokens.h"

namespace Fluent {

FluentButton::FluentButton(QWidget* parent)
    : QPushButton(parent)
{
    connectTheme();
    applyVariantStyle();
}

FluentButton::FluentButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    connectTheme();
    applyVariantStyle();
}

FluentButton::FluentButton(const QString& text, Variant variant, QWidget* parent)
    : QPushButton(text, parent), m_variant(variant)
{
    connectTheme();
    applyVariantStyle();
}

void FluentButton::setVariant(Variant v)
{
    if (m_variant == v) return;
    m_variant = v;
    applyVariantStyle();
}

void FluentButton::setIconText(const QString& iconChar, const QString& text)
{
    setText(iconChar + QStringLiteral("  ") + text);
}

void FluentButton::connectTheme()
{
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyVariantStyle();
    });
    connect(&ThemeManager::instance(), &ThemeManager::accentColorChanged, this, [this]() {
        applyVariantStyle();
    });
}

void FluentButton::applyVariantStyle()
{
    auto& tm = ThemeManager::instance();
    const int r = Radius::Medium;
    QString style;

    switch (m_variant) {
    case Primary:
        style = QStringLiteral(
            "QPushButton { background-color: %1; border: none; border-radius: %4px;"
            "  color: #ffffff; font-weight: 600; padding: 8px 20px; font-size: 13px; }"
            "QPushButton:hover { background-color: %2; }"
            "QPushButton:pressed { background-color: %3; }"
            "QPushButton:disabled { background-color: %5; color: %6; }")
            .arg(tm.accentPrimary(), tm.accentHover(), tm.accentActive())
            .arg(r)
            .arg(tm.surface(), tm.textDisabled());
        break;

    case Ghost:
        style = QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: %1px;"
            "  color: %2; font-weight: 500; padding: 8px 16px; font-size: 13px; }"
            "QPushButton:hover { background-color: %3; color: %4; }"
            "QPushButton:pressed { background-color: %5; }")
            .arg(r)
            .arg(tm.textSecondary(), tm.surface(), tm.textPrimary(), tm.border());
        break;

    case Danger:
        style = QStringLiteral(
            "QPushButton { background-color: %1; border: none; border-radius: %2px;"
            "  color: #ffffff; font-weight: 600; padding: 8px 20px; font-size: 13px; }"
            "QPushButton:hover { background-color: #dc2626; }"
            "QPushButton:pressed { background-color: #b91c1c; }")
            .arg(Accent::Error)
            .arg(r);
        break;

    case Secondary:
    default:
        style = QStringLiteral(
            "QPushButton { background-color: %1; border: 1px solid %2; border-radius: %5px;"
            "  color: %3; font-weight: 500; padding: 8px 16px; font-size: 13px; }"
            "QPushButton:hover { background-color: %4; border-color: %4; }"
            "QPushButton:pressed { background-color: %6; }"
            "QPushButton:disabled { background-color: %7; color: %8; border-color: %9; }")
            .arg(tm.surface(), tm.border(), tm.textPrimary(), tm.border())
            .arg(r)
            .arg(tm.borderSoft(), tm.card(), tm.textDisabled(), tm.borderSoft());
        break;
    }

    setStyleSheet(style);
}

} // namespace Fluent
