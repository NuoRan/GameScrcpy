#include "DiagnosticsOverlay.h"

#include <QColor>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>

#include <algorithm>

namespace gs::app {

namespace {

constexpr qreal kMargin = 12;
constexpr qreal kPadding = 12;
constexpr qreal kRadius = 10;
constexpr int kTextPixels = 12;
constexpr int kWrap = Qt::TextWordWrap | Qt::TextWrapAnywhere;
const QColor kPanel(0x0B, 0x0C, 0x0F, 0xC8);
const QColor kText(0xED, 0xEF, 0xF3);

}

DiagnosticsOverlay::DiagnosticsOverlay(QQuickItem* parent) : QQuickPaintedItem(parent), m_font(QGuiApplication::font())
{
    m_font.setPixelSize(kTextPixels);
    setPosition({kMargin, kMargin});
    setZ(1);
    setVisible(false);
    connect(parent, &QQuickItem::widthChanged, this, &DiagnosticsOverlay::layout);
}

void DiagnosticsOverlay::setTopInset(qreal inset)
{
    setY(kMargin + inset);
}

void DiagnosticsOverlay::setLines(const QStringList& lines)
{
    m_lines = lines;
    setVisible(!m_lines.isEmpty());
    layout();
}

void DiagnosticsOverlay::layout()
{
    const qreal available = std::max<qreal>(160, parentItem()->width() - 2 * kMargin) - 2 * kPadding;
    const QFontMetricsF metrics(m_font);
    m_lineRects.clear();
    qreal width = 0;
    qreal top = kPadding;
    for (const QString& line : m_lines) {
        const QRectF bounds = metrics.boundingRect(QRectF(0, 0, available, 1e6), kWrap, line);
        m_lineRects.push_back(QRectF(kPadding, top, available, bounds.height()));
        width = std::max(width, bounds.width());
        top += bounds.height();
    }
    setSize({width + 2 * kPadding, top + kPadding});
    update();
}

void DiagnosticsOverlay::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(kPanel);
    painter->drawRoundedRect(boundingRect(), kRadius, kRadius);
    painter->setFont(m_font);
    painter->setPen(kText);
    for (qsizetype i = 0; i < m_lines.size() && i < m_lineRects.size(); ++i) {
        painter->drawText(m_lineRects[i], kWrap, m_lines[i]);
    }
}

}
