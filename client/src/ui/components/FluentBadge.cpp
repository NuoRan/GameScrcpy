/**
 * @file FluentBadge.cpp
 */
#include "FluentBadge.h"
#include "ThemeManager.h"
#include <QPainter>

namespace Fluent {

FluentBadge::FluentBadge(QWidget* parent) : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void FluentBadge::setStatus(Status s) { m_status = s; update(); }
void FluentBadge::setText(const QString& t) { m_text = t; updateGeometry(); update(); }

QSize FluentBadge::sizeHint() const
{
    if (m_text.isEmpty()) return {12, 12};
    QFontMetrics fm(font());
    int w = fm.horizontalAdvance(m_text) + 12;
    return {qMax(w, 20), 18};
}

void FluentBadge::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor color;
    switch (m_status) {
    case Online:    color = QColor("#22c55e"); break;
    case Streaming: color = QColor(ThemeManager::instance().accentPrimary()); break;
    case Warning:   color = QColor("#f59e0b"); break;
    case Offline:
    default:        color = QColor("#71717a"); break;
    }

    if (m_text.isEmpty()) {
        // Dot badge
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(rect().adjusted(2, 2, -2, -2));
    } else {
        // Label badge
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(rect(), height() / 2, height() / 2);
        p.setPen(Qt::white);
        p.setFont(QFont(font().family(), 9, QFont::Bold));
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }
}

} // namespace Fluent
