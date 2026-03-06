/**
 * @file FluentSlider.cpp
 * @brief FluentSlider 实现
 */
#include "FluentSlider.h"
#include "ThemeManager.h"

#include <QPainter>
#include <QMouseEvent>

namespace Fluent {

FluentSlider::FluentSlider(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    m_scaleAnim = new QPropertyAnimation(this, "handleScale", this);
    m_scaleAnim->setDuration(100);
}

void FluentSlider::setRange(int min, int max) { m_min = min; m_max = max; update(); }
void FluentSlider::setValue(int v) { v = qBound(m_min, v, m_max); if (v != m_value) { m_value = v; update(); emit valueChanged(v); } }
void FluentSlider::setLabel(const QString& l) { m_label = l; update(); }
void FluentSlider::setShowValue(bool s) { m_showValue = s; update(); }
void FluentSlider::setSuffix(const QString& s) { m_suffix = s; update(); }

QRectF FluentSlider::trackRect() const
{
    qreal labelW = m_label.isEmpty() ? 0 : 58;
    qreal valueW = m_showValue ? 36 : 0;
    qreal x = labelW;
    qreal w = width() - labelW - valueW - 4;
    qreal y = height() / 2.0 - 2;
    return {x, y, w, 4};
}

QPointF FluentSlider::handleCenter() const
{
    auto tr = trackRect();
    qreal ratio = (m_max > m_min) ? qreal(m_value - m_min) / (m_max - m_min) : 0;
    return {tr.x() + tr.width() * ratio, tr.y() + tr.height() / 2.0};
}

int FluentSlider::valueFromX(int x) const
{
    auto tr = trackRect();
    qreal ratio = qBound(0.0, (x - tr.x()) / tr.width(), 1.0);
    return m_min + qRound(ratio * (m_max - m_min));
}

void FluentSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto& tm = ThemeManager::instance();

    // Label
    if (!m_label.isEmpty()) {
        p.setPen(QColor(tm.textSecondary()));
        p.setFont(QFont(font().family(), 9));
        p.drawText(QRectF(0, 0, 56, height()), Qt::AlignVCenter | Qt::AlignLeft, m_label);
    }

    auto tr = trackRect();
    auto hc = handleCenter();

    // Track background
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(tm.border()));
    p.drawRoundedRect(tr, 2, 2);

    // Track fill
    QRectF fill(tr.x(), tr.y(), hc.x() - tr.x(), tr.height());
    p.setBrush(QColor(tm.accentPrimary()));
    p.drawRoundedRect(fill, 2, 2);

    // Handle
    qreal baseR = 8;
    qreal r = baseR * m_handleScale;
    p.setPen(QPen(QColor(tm.accentPrimary()), 2));
    p.setBrush(Qt::white);
    p.drawEllipse(hc, r, r);

    // Value
    if (m_showValue) {
        p.setPen(QColor(tm.textPrimary()));
        p.setFont(QFont(font().family(), 9, QFont::Bold));
        qreal valueX = tr.right() + 8;
        p.drawText(QRectF(valueX, 0, 36, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::number(m_value) + m_suffix);
    }
}

void FluentSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        setValue(valueFromX(e->pos().x()));
    }
}

void FluentSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) setValue(valueFromX(e->pos().x()));
}

void FluentSlider::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

void FluentSlider::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    m_scaleAnim->stop();
    m_scaleAnim->setStartValue(m_handleScale);
    m_scaleAnim->setEndValue(1.25);
    m_scaleAnim->start();
}

void FluentSlider::leaveEvent(QEvent*)
{
    m_hovered = false;
    m_scaleAnim->stop();
    m_scaleAnim->setStartValue(m_handleScale);
    m_scaleAnim->setEndValue(1.0);
    m_scaleAnim->start();
}

} // namespace Fluent
