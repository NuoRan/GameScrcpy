#include "ThumbnailView.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace gs::app {

ThumbnailView::ThumbnailView(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void ThumbnailView::setImage(const QImage& image)
{
    m_image = image;
    update();
    emit imageChanged();
}

void ThumbnailView::setSource(const QUrl& source)
{
    if (source == m_source) {
        return;
    }
    m_source = source;
    setImage(source.isLocalFile() ? QImage(source.toLocalFile()) : QImage());
    emit sourceChanged();
}

void ThumbnailView::setRadius(qreal radius)
{
    if (qFuzzyCompare(radius, m_radius)) {
        return;
    }
    m_radius = radius;
    update();
    emit radiusChanged();
}

void ThumbnailView::paint(QPainter* painter)
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        return;
    }
    const QRectF target(0, 0, width(), height());
    QRectF source(m_image.rect());
    QRectF place = target;
    if (m_image.height() >= m_image.width()) {
        const qreal scale = std::max(target.width() / source.width(), target.height() / source.height());
        const QSizeF shown(target.width() / scale, target.height() / scale);
        source = QRectF(source.center() - QPointF(shown.width() / 2, shown.height() / 2), shown);
    } else {
        const qreal scale = std::min(target.width() / source.width(), target.height() / source.height());
        const QSizeF placed(source.width() * scale, source.height() * scale);
        place = QRectF(target.center() - QPointF(placed.width() / 2, placed.height() / 2), placed);
    }
    QPainterPath clip;
    clip.addRoundedRect(target, m_radius, m_radius);
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter->setClipPath(clip);
    painter->drawImage(place, m_image, source);
}

}
