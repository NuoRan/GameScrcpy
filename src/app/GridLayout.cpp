#include "GridLayout.h"

#include <algorithm>
#include <cmath>

namespace gs::app {

namespace {

constexpr double kTie = 1e-6;

double usableAspect(double aspect)
{
    return std::isfinite(aspect) && aspect > 0.0 ? aspect : kFallbackAspect;
}

QSizeF cellSize(QSizeF area, GridShape shape, double gap)
{
    return {(area.width() - gap * (shape.columns - 1)) / shape.columns,
            (area.height() - gap * (shape.rows - 1)) / shape.rows};
}

QSizeF pictureIn(QSizeF cell, const GridItem& item)
{
    const double width = cell.width() - item.chrome.width();
    const double height = cell.height() - item.chrome.height();
    if (!(width > 0.0) || !(height > 0.0)) {
        return {0.0, 0.0};
    }
    const double aspect = usableAspect(item.aspect);
    const double fittedHeight = std::min(height, width / aspect);
    return {fittedHeight * aspect, fittedHeight};
}

}

GridShape gridShape(QSizeF area, std::span<const GridItem> items, double gap)
{
    const int count = static_cast<int>(items.size());
    GridShape best;
    double bestArea = 0.0;
    int bestEmpty = 0;
    for (int columns = 1; columns <= count; ++columns) {
        const GridShape shape{.columns = columns, .rows = (count + columns - 1) / columns};
        const QSizeF cell = cellSize(area, shape, gap);
        double total = 0.0;
        for (const GridItem& item : items) {
            const QSizeF picture = pictureIn(cell, item);
            total += picture.width() * picture.height();
        }
        const int empty = shape.columns * shape.rows - count;
        const double tolerance = kTie * std::max(bestArea, 1.0);
        const bool larger = total > bestArea + tolerance;
        const bool tied = std::abs(total - bestArea) <= tolerance;
        if (best.columns == 0 || larger || (tied && empty < bestEmpty)) {
            best = shape;
            bestArea = total;
            bestEmpty = empty;
        }
    }
    return best;
}

std::vector<QRectF> gridCells(const QRectF& area, int count, GridShape shape, double gap)
{
    std::vector<QRectF> cells;
    if (count <= 0 || shape.columns <= 0 || shape.rows <= 0) {
        return cells;
    }
    const QSizeF cell = cellSize(area.size(), shape, gap);
    cells.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const int row = index / shape.columns;
        const int column = index % shape.columns;
        const int inRow = std::min(shape.columns, count - row * shape.columns);
        const double shift = (shape.columns - inRow) * (cell.width() + gap) / 2.0;
        cells.emplace_back(area.x() + shift + column * (cell.width() + gap), area.y() + row * (cell.height() + gap),
                           cell.width(), cell.height());
    }
    return cells;
}

QRectF fittedPicture(const QRectF& bounds, double aspect)
{
    if (!(bounds.width() > 0.0) || !(bounds.height() > 0.0)) {
        return {};
    }
    const double usable = usableAspect(aspect);
    const double height = std::min(bounds.height(), bounds.width() / usable);
    const double width = height * usable;
    return {bounds.x() + (bounds.width() - width) / 2.0, bounds.y() + (bounds.height() - height) / 2.0, width,
            height};
}

std::vector<QRect> tileWindows(const QRect& area, std::span<const GridItem> windows, int gap)
{
    const GridShape shape = gridShape(QSizeF(area.size()), windows, gap);
    const std::vector<QRectF> cells = gridCells(QRectF(area), static_cast<int>(windows.size()), shape, gap);
    std::vector<QRect> rects;
    rects.reserve(cells.size());
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const QRectF& cell = cells[index];
        const GridItem& window = windows[index];
        const QSizeF picture = pictureIn(cell.size(), window);
        if (!(picture.width() > 0.0) || !(picture.height() > 0.0)) {
            rects.emplace_back();
            continue;
        }
        const double width = picture.width() + window.chrome.width();
        const double height = picture.height() + window.chrome.height();
        const double x = cell.x() + (cell.width() - width) / 2.0;
        const double y = cell.y() + (cell.height() - height) / 2.0;
        const int left = static_cast<int>(std::ceil(x));
        const int top = static_cast<int>(std::ceil(y));
        const int right = static_cast<int>(std::floor(x + width));
        const int bottom = static_cast<int>(std::floor(y + height));
        rects.emplace_back(left, top, std::max(0, right - left), std::max(0, bottom - top));
    }
    return rects;
}

QSize gridWindowSize(QSize workArea, std::span<const GridItem> items, const GridFrame& frame, double heightShare)
{
    if (items.empty() || workArea.isEmpty()) {
        return {};
    }
    const double wide = workArea.width();
    const double high = workArea.height() * std::clamp(heightShare, 0.0, 1.0);
    const double aroundWidth = 2.0 * frame.margin;
    const double aroundHeight = frame.top + 2.0 * frame.margin;
    const GridShape shape = gridShape(QSizeF(wide - aroundWidth, high - aroundHeight), items, frame.gap);
    if (shape.columns <= 0 || shape.rows <= 0) {
        return {};
    }
    double widest = usableAspect(items.front().aspect);
    double narrowest = widest;
    QSizeF chrome(0.0, 0.0);
    for (const GridItem& item : items) {
        const double aspect = usableAspect(item.aspect);
        widest = std::max(widest, aspect);
        narrowest = std::min(narrowest, aspect);
        chrome = chrome.expandedTo(item.chrome);
    }
    const double gapsAcross = frame.gap * (shape.columns - 1);
    const double gapsDown = frame.gap * (shape.rows - 1);
    double picture = (high - aroundHeight - gapsDown) / shape.rows - chrome.height();
    double cellWidth = picture * widest + chrome.width();
    const double roomyCell = (wide - aroundWidth - gapsAcross) / shape.columns;
    if (cellWidth > roomyCell) {
        cellWidth = roomyCell;
        picture = std::min(picture, (cellWidth - chrome.width()) / narrowest);
    }
    if (!(picture > 0.0)) {
        return workArea;
    }
    const double windowWidth = shape.columns * cellWidth + gapsAcross + aroundWidth;
    const double windowHeight = shape.rows * (picture + chrome.height()) + gapsDown + aroundHeight;
    return {std::min(workArea.width(), static_cast<int>(std::ceil(windowWidth - kTie))),
            std::min(workArea.height(), static_cast<int>(std::ceil(windowHeight - kTie)))};
}

}
