#pragma once

#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <algorithm>
#include <cmath>

namespace gs::render {

inline QRectF fitted(const QSize& picture, const QRectF& bounds)
{
    if (picture.isEmpty() || bounds.isEmpty()) {
        return {};
    }
    const qreal scale = std::min(bounds.width() / picture.width(), bounds.height() / picture.height());
    const QSizeF size(picture.width() * scale, picture.height() * scale);
    return {bounds.x() + (bounds.width() - size.width()) / 2, bounds.y() + (bounds.height() - size.height()) / 2,
            size.width(), size.height()};
}

inline QSize croppedSize(const QSize& frame, const QRectF& crop)
{
    if (frame.isEmpty()) {
        return frame;
    }
    return {std::max(1, static_cast<int>(std::lround(frame.width() * crop.width()))),
            std::max(1, static_cast<int>(std::lround(frame.height() * crop.height())))};
}

inline QRectF normalCrop(const QRectF& crop)
{
    const qreal left = std::clamp(crop.x(), 0.0, 1.0);
    const qreal top = std::clamp(crop.y(), 0.0, 1.0);
    const qreal right = std::clamp(crop.x() + crop.width(), left, 1.0);
    const qreal bottom = std::clamp(crop.y() + crop.height(), top, 1.0);
    if (right - left < 0.01 || bottom - top < 0.01) {
        return {0.0, 0.0, 1.0, 1.0};
    }
    return {left, top, right - left, bottom - top};
}

}
