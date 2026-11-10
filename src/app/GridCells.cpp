#include "GridCells.h"

#include <algorithm>
#include <cmath>

namespace gs::app {

namespace {

constexpr double kAspectStep = 0.01;

}

bool GridCells::add(const QString& serial, const QString& name, double aspect, bool separate)
{
    if (serial.isEmpty() || find(serial) != nullptr) {
        return false;
    }
    m_cells.push_back(Cell{.serial = serial, .name = name, .aspect = aspect, .separate = separate});
    return true;
}

bool GridCells::remove(const QString& serial)
{
    const auto found = std::ranges::find(m_cells, serial, &Cell::serial);
    if (found == m_cells.end()) {
        return false;
    }
    m_cells.erase(found);
    if (m_target == serial) {
        m_target.clear();
    }
    if (m_last && m_last->serial == serial) {
        m_last.reset();
    }
    return true;
}

const GridCells::Cell* GridCells::find(const QString& serial) const
{
    const auto found = std::ranges::find(m_cells, serial, &Cell::serial);
    return found != m_cells.end() ? &*found : nullptr;
}

GridCells::Cell* GridCells::cellOf(const QString& serial)
{
    const auto found = std::ranges::find(m_cells, serial, &Cell::serial);
    return found != m_cells.end() ? &*found : nullptr;
}

QStringList GridCells::serials() const
{
    QStringList list;
    list.reserve(static_cast<qsizetype>(m_cells.size()));
    for (const Cell& cell : m_cells) {
        list.append(cell.serial);
    }
    return list;
}

bool GridCells::setAspect(const QString& serial, double aspect)
{
    Cell* cell = cellOf(serial);
    if (cell == nullptr || !std::isfinite(aspect) || aspect <= 0.0) {
        return false;
    }
    if (cell->aspect > 0.0 && std::abs(aspect - cell->aspect) <= kAspectStep * cell->aspect) {
        return false;
    }
    cell->aspect = aspect;
    return true;
}

bool GridCells::setLabel(const QString& serial, const QString& name, const QString& tone, const QString& status)
{
    Cell* cell = cellOf(serial);
    if (cell == nullptr || (cell->name == name && cell->tone == tone && cell->status == status)) {
        return false;
    }
    cell->name = name;
    cell->tone = tone;
    cell->status = status;
    return true;
}

bool GridCells::setRate(const QString& serial, double framesPerSecond)
{
    Cell* cell = cellOf(serial);
    if (cell == nullptr) {
        return false;
    }
    const double rate =
        std::isfinite(framesPerSecond) && framesPerSecond >= 0.0 ? std::round(framesPerSecond) : -1.0;
    if (rate == cell->rate) {
        return false;
    }
    cell->rate = rate;
    return true;
}

bool GridCells::setSync(const QString& serial, const Sync& sync)
{
    Cell* cell = cellOf(serial);
    if (cell == nullptr || cell->sync == sync) {
        return false;
    }
    cell->sync = sync;
    return true;
}

bool GridCells::setSeparate(const QString& serial, bool separate)
{
    Cell* cell = cellOf(serial);
    if (cell == nullptr || cell->separate == separate) {
        return false;
    }
    cell->separate = separate;
    if (separate && m_target == serial) {
        m_target.clear();
    }
    m_last.reset();
    return true;
}

bool GridCells::setTarget(const QString& serial)
{
    if (!serial.isEmpty()) {
        const Cell* cell = find(serial);
        if (cell == nullptr || cell->separate) {
            return false;
        }
    }
    if (serial == m_target) {
        return false;
    }
    m_target = serial;
    return true;
}

std::size_t GridCells::knownAspects() const
{
    return static_cast<std::size_t>(std::ranges::count_if(
        m_cells, [](const Cell& cell) { return std::isfinite(cell.aspect) && cell.aspect > 0.0; }));
}

std::vector<GridItem> GridCells::items(const Metrics& metrics) const
{
    std::vector<GridItem> list;
    list.reserve(m_cells.size());
    for (const Cell& cell : m_cells) {
        list.push_back({.aspect = cell.aspect, .chrome = QSizeF(2.0 * metrics.inset, metrics.header + metrics.inset)});
    }
    return list;
}

void GridCells::layout(const QRectF& area, const Metrics& metrics)
{
    const QRectF inner = area.adjusted(metrics.margin, metrics.margin, -metrics.margin, -metrics.margin);
    m_shape = gridShape(inner.size(), items(metrics), metrics.gap);
    const std::vector<QRectF> rects = gridCells(inner, static_cast<int>(m_cells.size()), m_shape, metrics.gap);
    for (std::size_t index = 0; index < m_cells.size() && index < rects.size(); ++index) {
        Cell& cell = m_cells[index];
        const QRectF& rect = rects[index];
        cell.area = rect;
        cell.header = QRectF(rect.x(), rect.y(), rect.width(), std::min(metrics.header, rect.height()));
        const double menuWidth = std::max(0.0, std::min(metrics.menu, rect.width()));
        cell.menu = QRectF(rect.right() - menuWidth, rect.y(), menuWidth, cell.header.height());
        cell.view = QRectF(rect.x() + metrics.inset, rect.y() + metrics.header,
                           std::max(0.0, rect.width() - 2.0 * metrics.inset),
                           std::max(0.0, rect.height() - metrics.header - metrics.inset));
    }
}

QSize GridCells::windowSize(QSize workArea, double top, const Metrics& metrics, double heightShare) const
{
    return gridWindowSize(workArea, items(metrics), {.top = top, .margin = metrics.margin, .gap = metrics.gap},
                          heightShare);
}

void GridCells::setDoubleClick(std::int64_t intervalMs, double distance)
{
    m_doubleClickMs = std::max<std::int64_t>(intervalMs, 0);
    m_doubleClickDistance = std::max(distance, 0.0);
}

GridCells::Press GridCells::press(QPointF at, Qt::MouseButton button, std::int64_t timeMs)
{
    const auto found = std::ranges::find_if(m_cells, [at](const Cell& each) { return each.area.contains(at); });
    if (found == m_cells.end()) {
        m_last.reset();
        return {};
    }
    const Cell& cell = *found;
    if (cell.menu.contains(at)) {
        m_last.reset();
        return {.action = Action::Menu, .serial = cell.serial};
    }
    const bool header = cell.header.contains(at);
    const bool repeated = repeats(cell.serial, at, button, timeMs);
    if (cell.separate) {
        if (repeated) {
            m_last.reset();
            return {.action = Action::BackToGrid, .serial = cell.serial};
        }
        remember(cell, at, button, timeMs, header, false);
        return {.action = Action::None, .serial = cell.serial};
    }
    if (cell.serial != m_target) {
        m_target = cell.serial;
        remember(cell, at, button, timeMs, header, true);
        return {.action = Action::Select, .serial = cell.serial};
    }
    if (repeated && (m_last->selecting || (header && m_last->header))) {
        m_last.reset();
        return {.action = Action::OwnWindow, .serial = cell.serial};
    }
    remember(cell, at, button, timeMs, header, false);
    return {.action = header ? Action::None : Action::Forward, .serial = cell.serial};
}

bool GridCells::repeats(const QString& serial, QPointF at, Qt::MouseButton button, std::int64_t timeMs) const
{
    return button == Qt::LeftButton && m_last && m_last->serial == serial && timeMs >= m_last->timeMs &&
           timeMs - m_last->timeMs <= m_doubleClickMs &&
           std::hypot(at.x() - m_last->at.x(), at.y() - m_last->at.y()) <= m_doubleClickDistance;
}

void GridCells::remember(const Cell& cell, QPointF at, Qt::MouseButton button, std::int64_t timeMs, bool header,
                         bool selecting)
{
    if (button != Qt::LeftButton) {
        m_last.reset();
        return;
    }
    m_last = LastPress{.serial = cell.serial, .at = at, .timeMs = timeMs, .header = header, .selecting = selecting};
}

GridHints gridHints(const GridCells& cells, bool keymap, bool textInput, bool foreground, const QString& wheelSerial)
{
    const GridCells::Cell* cell = cells.find(cells.target());
    if (cell == nullptr || cell->separate) {
        return {};
    }
    return {.serial = cell->serial,
            .keyHints = keymap && !textInput,
            .wheel = foreground && wheelSerial == cell->serial};
}

}
