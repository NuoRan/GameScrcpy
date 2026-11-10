#include "WindowFit.h"

namespace gs::app {

namespace {

constexpr int kGripWidth = 120;
constexpr int kGripHeight = 32;

}

QRect fitWindow(const settings::WindowPlacement& saved, const QList<QRect>& screens, QSize fallback, QSize minimum)
{
    const bool placed = saved.width > 0 && saved.height > 0;
    QSize size = placed ? QSize(saved.width, saved.height) : fallback;
    if (screens.isEmpty()) {
        return {QPoint(saved.x, saved.y), size.expandedTo(minimum)};
    }
    if (placed) {
        const QRect grip(saved.x, saved.y, size.width(), kGripHeight);
        for (const QRect& screen : screens) {
            const QRect reachable = grip.intersected(screen);
            if (reachable.width() >= kGripWidth && reachable.height() >= kGripHeight) {
                return {QPoint(saved.x, saved.y), size.boundedTo(screen.size()).expandedTo(minimum)};
            }
        }
    }
    const QRect& primary = screens.front();
    size = size.boundedTo(primary.size()).expandedTo(minimum);
    return {primary.x() + (primary.width() - size.width()) / 2, primary.y() + (primary.height() - size.height()) / 2,
            size.width(), size.height()};
}

}
