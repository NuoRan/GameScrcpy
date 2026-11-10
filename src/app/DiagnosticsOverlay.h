#pragma once

#include <QFont>
#include <QList>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QStringList>

namespace gs::app {

class DiagnosticsOverlay final : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit DiagnosticsOverlay(QQuickItem* parent);

    void setLines(const QStringList& lines);
    void setTopInset(qreal inset);
    void paint(QPainter* painter) override;

private:
    void layout();

    QFont m_font;
    QStringList m_lines;
    QList<QRectF> m_lineRects;
};

}
