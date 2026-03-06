/**
 * @file FluentToggle.cpp
 * @brief FluentToggle 实现 — 带动画的滑块 Toggle
 */

#include "FluentToggle.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPropertyAnimation>

namespace Fluent {

static constexpr int ToggleWidth  = 40;
static constexpr int ToggleHeight = 22;
static constexpr int HandleSize   = 16;
static constexpr int HandleMargin = 3;

FluentToggle::FluentToggle(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(ToggleWidth, ToggleHeight);
    setCursor(Qt::PointingHandCursor);
}

QSize FluentToggle::sizeHint() const { return {ToggleWidth, ToggleHeight}; }
QSize FluentToggle::minimumSizeHint() const { return sizeHint(); }

void FluentToggle::setChecked(bool checked)
{
    if (m_checked == checked) return;
    m_checked = checked;
    animateToggle();
    emit toggled(m_checked);
}

void FluentToggle::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto& tm = ThemeManager::instance();
    const QColor trackColor = m_checked
        ? QColor(tm.accentPrimary())
        : QColor(tm.surface());
    const QColor borderColor = m_checked
        ? QColor(tm.accentPrimary())
        : QColor(tm.border());
    const QColor handleColor(m_checked ? "#ffffff" : tm.textSecondary());

    // 绘制轨道
    const qreal trackRadius = ToggleHeight / 2.0;
    p.setPen(QPen(borderColor, 1.5));
    p.setBrush(trackColor);
    p.drawRoundedRect(QRectF(0.5, 0.5, ToggleWidth - 1, ToggleHeight - 1), trackRadius, trackRadius);

    // 绘制手柄
    const qreal handleX = HandleMargin + m_handlePos * (ToggleWidth - HandleSize - HandleMargin * 2);
    const qreal handleY = (ToggleHeight - HandleSize) / 2.0;
    p.setPen(Qt::NoPen);
    p.setBrush(handleColor);
    p.drawEllipse(QRectF(handleX, handleY, HandleSize, HandleSize));
}

void FluentToggle::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);
    event->accept();
}

void FluentToggle::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
    if (rect().contains(event->pos())) {
        setChecked(!m_checked);
    }
}

void FluentToggle::setHandlePosition(qreal pos)
{
    m_handlePos = pos;
    update();
}

void FluentToggle::animateToggle()
{
    auto* anim = new QPropertyAnimation(this, "handlePosition", this);
    anim->setDuration(Motion::duration(Motion::Normal));
    anim->setStartValue(m_handlePos);
    anim->setEndValue(m_checked ? 1.0 : 0.0);
    anim->setEasingCurve(Motion::defaultCurve());
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace Fluent
