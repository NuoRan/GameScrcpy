/**
 * @file FluentProgressRing.cpp
 */
#include "FluentProgressRing.h"
#include "ThemeManager.h"
#include <QPainter>

namespace Fluent {

FluentProgressRing::FluentProgressRing(QWidget* parent) : QWidget(parent)
{
    m_rotAnim = new QPropertyAnimation(this, "rotation", this);
    m_rotAnim->setDuration(1200);
    m_rotAnim->setStartValue(0.0);
    m_rotAnim->setEndValue(360.0);
    m_rotAnim->setLoopCount(-1);

    m_arcAnim = new QPropertyAnimation(this, "arcLength", this);
    m_arcAnim->setDuration(1200);
    m_arcAnim->setKeyValueAt(0, 20.0);
    m_arcAnim->setKeyValueAt(0.5, 240.0);
    m_arcAnim->setKeyValueAt(1.0, 20.0);
    m_arcAnim->setLoopCount(-1);

    setIndeterminate(true);
}

void FluentProgressRing::setIndeterminate(bool ind)
{
    m_indeterminate = ind;
    if (ind) { m_rotAnim->start(); m_arcAnim->start(); }
    else { m_rotAnim->stop(); m_arcAnim->stop(); }
    update();
}

void FluentProgressRing::setValue(int v)
{
    m_value = qBound(0, v, 100);
    if (!m_indeterminate) update();
}

void FluentProgressRing::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto& tm = ThemeManager::instance();

    int s = qMin(width(), height());
    QRectF r(m_strokeWidth / 2.0, m_strokeWidth / 2.0, s - m_strokeWidth, s - m_strokeWidth);
    r.moveCenter(QPointF(width() / 2.0, height() / 2.0));

    // Background ring
    p.setPen(QPen(QColor(tm.border()), m_strokeWidth, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r);

    // Foreground arc
    p.setPen(QPen(QColor(tm.accentPrimary()), m_strokeWidth, Qt::SolidLine, Qt::RoundCap));
    if (m_indeterminate) {
        int startAngle = int(m_rotation * 16);
        int spanAngle = int(m_arcLength * 16);
        p.drawArc(r, startAngle, spanAngle);
    } else {
        int spanAngle = int(m_value * 3.6 * 16);
        p.drawArc(r, 90 * 16, -spanAngle);
    }
}

} // namespace Fluent
