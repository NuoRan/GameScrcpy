/**
 * @file FluentCard.cpp
 * @brief FluentCard 实现
 */

#include "FluentCard.h"
#include "ThemeManager.h"
#include "DesignTokens.h"

#include <QVBoxLayout>
#include <QMouseEvent>

namespace Fluent {

FluentCard::FluentCard(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("FluentCard"));
    applyStyle();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyStyle();
    });
}

void FluentCard::setClickable(bool v)
{
    m_clickable = v;
    setCursor(v ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (v) m_hoverable = true;
}

void FluentCard::setPadding(int padding)
{
    if (!layout()) {
        auto* l = new QVBoxLayout(this);
        setLayout(l);
    }
    layout()->setContentsMargins(padding, padding, padding, padding);
}

void FluentCard::setPadding(int h, int v)
{
    if (!layout()) {
        auto* l = new QVBoxLayout(this);
        setLayout(l);
    }
    layout()->setContentsMargins(h, v, h, v);
}

void FluentCard::applyStyle()
{
    auto& tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "FluentCard, #FluentCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}")
        .arg(tm.card())
        .arg(tm.borderSoft())
        .arg(Radius::Large));
}

void FluentCard::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    if (m_hoverable) {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QStringLiteral(
            "FluentCard, #FluentCard {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: %3px;"
            "}")
            .arg(tm.surface())
            .arg(tm.border())
            .arg(Radius::Large));
    }
}

void FluentCard::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    if (m_hoverable) {
        applyStyle();
    }
}

void FluentCard::mousePressEvent(QMouseEvent* event)
{
    QFrame::mousePressEvent(event);
    if (m_clickable) {
        m_pressed = true;
    }
}

void FluentCard::mouseReleaseEvent(QMouseEvent* event)
{
    QFrame::mouseReleaseEvent(event);
    if (m_clickable && m_pressed) {
        m_pressed = false;
        if (rect().contains(event->pos())) {
            emit clicked();
        }
    }
}

} // namespace Fluent
