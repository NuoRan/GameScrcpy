#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <span>
#include <vector>

namespace gs::app {

inline constexpr double kFallbackAspect = 9.0 / 16.0;

struct GridShape {
    int columns = 0;
    int rows = 0;

    friend bool operator==(const GridShape&, const GridShape&) = default;
};

struct GridItem {
    double aspect = 0;
    QSizeF chrome{0.0, 0.0};
};

struct GridFrame {
    double top = 0;
    double margin = 0;
    double gap = 0;
};

[[nodiscard]] GridShape gridShape(QSizeF area, std::span<const GridItem> items, double gap);
[[nodiscard]] std::vector<QRectF> gridCells(const QRectF& area, int count, GridShape shape, double gap);
[[nodiscard]] QRectF fittedPicture(const QRectF& bounds, double aspect);
[[nodiscard]] std::vector<QRect> tileWindows(const QRect& area, std::span<const GridItem> windows, int gap);
[[nodiscard]] QSize gridWindowSize(QSize workArea, std::span<const GridItem> items, const GridFrame& frame,
                                   double heightShare);

}
