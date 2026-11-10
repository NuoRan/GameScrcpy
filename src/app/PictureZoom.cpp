#include "PictureZoom.h"

#include <algorithm>

namespace gs::app {

namespace {

double place(double start, double room, double length, double& offset)
{
    if (length <= room) {
        offset = 0;
        return start + (room - length) / 2;
    }
    offset = std::clamp(offset, 0.0, length - room);
    return start - offset;
}

}

ZoomedPicture zoomedPicture(const QRectF& view, QSizeF frame, double zoom, QPointF scroll)
{
    if (view.isEmpty() || frame.isEmpty() || !(zoom > 0)) {
        return {.picture = view, .scroll = {}};
    }
    const double fit = std::min(view.width() / frame.width(), view.height() / frame.height());
    const QSizeF size = frame * (fit * zoom);
    const double x = place(view.x(), view.width(), size.width(), scroll.rx());
    const double y = place(view.y(), view.height(), size.height(), scroll.ry());
    return {.picture = QRectF(QPointF(x, y), size), .scroll = scroll};
}

QPointF scrollKeeping(const QRectF& view, QSizeF frame, double from, double to, QPointF scroll, QPointF anchor)
{
    const QRectF before = zoomedPicture(view, frame, from, scroll).picture;
    if (before.isEmpty() || !(from > 0) || !(to > 0)) {
        return scroll;
    }
    const double factor = to / from;
    const QPointF corner(anchor.x() - (anchor.x() - before.x()) * factor,
                         anchor.y() - (anchor.y() - before.y()) * factor);
    return view.topLeft() - corner;
}

}
