#include "gs/input/Geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace gs::input {

std::uint16_t fixedCoordinate(double normalized) noexcept
{
    return static_cast<std::uint16_t>(std::lround(std::clamp(normalized, 0.0, 1.0) * 65535.0));
}

namespace {

Vec2 anchorFraction(Anchor anchor) noexcept
{
    switch (anchor) {
    case Anchor::TopLeft: return {0.0, 0.0};
    case Anchor::Top: return {0.5, 0.0};
    case Anchor::TopRight: return {1.0, 0.0};
    case Anchor::Left: return {0.0, 0.5};
    case Anchor::Center: return {0.5, 0.5};
    case Anchor::Right: return {1.0, 0.5};
    case Anchor::BottomLeft: return {0.0, 1.0};
    case Anchor::Bottom: return {0.5, 1.0};
    case Anchor::BottomRight: return {1.0, 1.0};
    }
    return {0.5, 0.5};
}

}

CoordinateMapper::CoordinateMapper(const ScreenGeometry& screen, ScaleMode scale, SafeArea safeArea) noexcept
    : m_screenWidth(std::max(screen.width, 1.0))
    , m_screenHeight(std::max(screen.height, 1.0))
    , m_left(safeArea == SafeArea::Respect ? screen.safeLeft : 0.0)
    , m_top(safeArea == SafeArea::Respect ? screen.safeTop : 0.0)
    , m_width(m_screenWidth - (safeArea == SafeArea::Respect ? screen.safeLeft + screen.safeRight : 0.0))
    , m_height(m_screenHeight - (safeArea == SafeArea::Respect ? screen.safeTop + screen.safeBottom : 0.0))
    , m_unitX(scale == ScaleMode::ShortSide ? std::min(m_width, m_height) : m_width)
    , m_unitY(scale == ScaleMode::ShortSide ? std::min(m_width, m_height) : m_height)
{
}

Vec2 CoordinateMapper::position(Anchor anchor, Vec2 offset) const noexcept
{
    const Vec2 fraction = anchorFraction(anchor);
    const double x = m_left + fraction.x * m_width + offset.x * m_unitX;
    const double y = m_top + fraction.y * m_height + offset.y * m_unitY;
    return {x / m_screenWidth, y / m_screenHeight};
}

Vec2 CoordinateMapper::extent(Vec2 size) const noexcept
{
    return {size.x * m_unitX / m_screenWidth, size.y * m_unitY / m_screenHeight};
}

Vec2 CoordinateMapper::offsetOf(Anchor anchor, Vec2 position) const noexcept
{
    const Vec2 fraction = anchorFraction(anchor);
    return {(position.x * m_screenWidth - m_left - fraction.x * m_width) / m_unitX,
            (position.y * m_screenHeight - m_top - fraction.y * m_height) / m_unitY};
}

Vec2 CoordinateMapper::sizeOf(Vec2 extent) const noexcept
{
    return {extent.x * m_screenWidth / m_unitX, extent.y * m_screenHeight / m_unitY};
}

Anchor CoordinateMapper::anchorAt(Vec2 position) const noexcept
{
    const auto third = [](double value, double start, double length) {
        const double share = (value - start) / length;
        return share < 1.0 / 3.0 ? 0 : share < 2.0 / 3.0 ? 1 : 2;
    };
    const int column = third(position.x * m_screenWidth, m_left, m_width);
    const int row = third(position.y * m_screenHeight, m_top, m_height);
    constexpr std::array<Anchor, 9> anchors{Anchor::TopLeft,    Anchor::Top,    Anchor::TopRight,
                                            Anchor::Left,       Anchor::Center, Anchor::Right,
                                            Anchor::BottomLeft, Anchor::Bottom, Anchor::BottomRight};
    return anchors[static_cast<std::size_t>(row * 3 + column)];
}

}
