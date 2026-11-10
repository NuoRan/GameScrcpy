#pragma once

#include <cstdint>

namespace gs::input {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Vec2&, const Vec2&) = default;
};

[[nodiscard]] std::uint16_t fixedCoordinate(double normalized) noexcept;

enum class Anchor : std::uint8_t { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };

enum class ScaleMode : std::uint8_t {
    ShortSide,
    Stretch,
};

enum class SafeArea : std::uint8_t { Ignore, Respect };

struct ScreenGeometry {
    double width = 0.0;
    double height = 0.0;
    double safeLeft = 0.0;
    double safeTop = 0.0;
    double safeRight = 0.0;
    double safeBottom = 0.0;
};

class CoordinateMapper
{
public:
    CoordinateMapper(const ScreenGeometry& screen, ScaleMode scale, SafeArea safeArea) noexcept;

    [[nodiscard]] Vec2 position(Anchor anchor, Vec2 offset) const noexcept;

    [[nodiscard]] Vec2 extent(Vec2 size) const noexcept;

    [[nodiscard]] Vec2 radius(double r) const noexcept { return extent({r, r}); }

    [[nodiscard]] Vec2 offsetOf(Anchor anchor, Vec2 position) const noexcept;

    [[nodiscard]] Vec2 sizeOf(Vec2 extent) const noexcept;

    [[nodiscard]] Anchor anchorAt(Vec2 position) const noexcept;

private:
    double m_screenWidth;
    double m_screenHeight;
    double m_left;
    double m_top;
    double m_width;
    double m_height;
    double m_unitX;
    double m_unitY;
};

}
