/**
 * @file FluentComboBox.cpp
 * @brief Fluent Focus 下拉框实现
 */
#include "FluentComboBox.h"
#include "../theme/DesignTokens.h"
#include "../theme/MotionTokens.h"

#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QListView>
#include <QLineEdit>
#include <QAbstractItemView>
#include <QWheelEvent>

namespace Fluent {

FluentComboBox::FluentComboBox(QWidget* parent)
    : QComboBox(parent)
{
    applyStyle();

    // Elevation-2 shadow on dropdown
    if (auto* v = view()) {
        auto* shadow = new QGraphicsDropShadowEffect(v);
        shadow->setBlurRadius(Shadow::CardBlur);
        shadow->setOffset(0, Shadow::CardOffsetY);
        shadow->setColor(QColor(0, 0, 0, 80));
        v->setGraphicsEffect(shadow);
    }

    m_anim = new QPropertyAnimation(this, "dropOpacity", this);
    m_anim->setDuration(Motion::Fast);
    m_anim->setEasingCurve(Motion::defaultCurve());
}

void FluentComboBox::setSearchable(bool enabled)
{
    m_searchable = enabled;
    setEditable(enabled);
    if (enabled) {
        lineEdit()->setPlaceholderText(tr("搜索..."));
        setInsertPolicy(QComboBox::NoInsert);
        // re-apply style for editable mode
        applyStyle();
    }
}

void FluentComboBox::showPopup()
{
    m_anim->stop();
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
    QComboBox::showPopup();
}

void FluentComboBox::hidePopup()
{
    QComboBox::hidePopup();
    m_dropOpacity = 0.0;
}

void FluentComboBox::paintEvent(QPaintEvent* e)
{
    QComboBox::paintEvent(e);

    // Draw accent indicator on left of current item when focused
    if (hasFocus()) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(Accent::Primary));
        int indicatorW = 3;
        int indicatorH = qMin(16, height() - 8);
        int y = (height() - indicatorH) / 2;
        p.drawRoundedRect(QRect(1, y, indicatorW, indicatorH), 1, 1);
    }
}

void FluentComboBox::wheelEvent(QWheelEvent* e)
{
    // Prevent accidental value changes on scroll
    if (!hasFocus()) {
        e->ignore();
        return;
    }
    QComboBox::wheelEvent(e);
}

void FluentComboBox::setDropOpacity(qreal v)
{
    m_dropOpacity = v;
    if (auto* v2 = view()) {
        v2->setWindowOpacity(m_dropOpacity);
    }
}

void FluentComboBox::applyStyle()
{
    QString style = QString(
        "FluentComboBox {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %4px;"
        "  padding: 4px 28px 4px 8px;"
        "  font-size: %5px;"
        "  min-height: 24px;"
        "}"
        "FluentComboBox:hover {"
        "  border-color: %6;"
        "}"
        "FluentComboBox:focus {"
        "  border-color: %7;"
        "  border-bottom: 2px solid %7;"
        "}"
        "FluentComboBox::drop-down {"
        "  border: none;"
        "  width: 24px;"
        "}"
        "FluentComboBox::down-arrow {"
        "  image: none;"
        "  width: 12px; height: 12px;"
        "}"
        "FluentComboBox QAbstractItemView {"
        "  background: %8;"
        "  border: 1px solid %9;"
        "  border-radius: %10px;"
        "  selection-background-color: %11;"
        "  selection-color: %12;"
        "  padding: 4px;"
        "  outline: none;"
        "}"
        "FluentComboBox QAbstractItemView::item {"
        "  padding: 6px 8px;"
        "  border-radius: %13px;"
        "  min-height: 28px;"
        "}"
        "FluentComboBox QAbstractItemView::item:hover {"
        "  background: %14;"
        "}"
    ).arg(
        Dark::InputBg, Dark::TextPrimary, Dark::InputBorder
    ).arg(
        Radius::Small
    ).arg(
        Font::Body
    ).arg(
        Dark::TextSecondary, Accent::Primary
    ).arg(
        Dark::Card, Dark::Border
    ).arg(
        Radius::Medium
    ).arg(
        Dark::Surface, Dark::TextPrimary
    ).arg(
        Radius::Small
    ).arg(
        Dark::NavHover
    );

    setStyleSheet(style);
}

} // namespace Fluent
