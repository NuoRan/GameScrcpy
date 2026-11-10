#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace gs::app {

struct ZoomedPicture {
    QRectF picture;
    QPointF scroll;
};

[[nodiscard]] ZoomedPicture zoomedPicture(const QRectF& view, QSizeF frame, double zoom, QPointF scroll);

[[nodiscard]] QPointF scrollKeeping(const QRectF& view, QSizeF frame, double from, double to, QPointF scroll,
                                    QPointF anchor);

}
