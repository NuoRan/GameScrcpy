#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace gs::capture {

struct CropBox {
    double x = 0.0;
    double y = 0.0;
    double width = 1.0;
    double height = 1.0;

    [[nodiscard]] bool whole(double slack = 0.001) const noexcept;
    [[nodiscard]] bool contains(const CropBox& other, double slack) const noexcept;
    [[nodiscard]] bool closeTo(const CropBox& other, double slack) const noexcept;

    friend bool operator==(const CropBox&, const CropBox&) = default;
};

struct PixelBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const PixelBox&, const PixelBox&) = default;
};

struct LumaPlane {
    const std::uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    std::ptrdiff_t pitch = 0;
};

struct CropOptions {
    int threshold = 32;
    std::chrono::milliseconds hold{4000};
    std::chrono::milliseconds stable{1000};
    double slack = 0.01;
    double fill = 0.98;
    double narrowest = 1.1;
    double widest = 2.8;
};

class CropDetector
{
public:
    using Clock = std::chrono::steady_clock;

    explicit CropDetector(CropOptions options = {});

    bool feed(const LumaPlane& plane, Clock::time_point at);
    void reset();
    [[nodiscard]] const std::optional<CropBox>& box() const noexcept { return m_accepted; }

private:
    struct Peaks {
        Clock::time_point at;
        std::vector<std::uint8_t> rows;
        std::vector<std::uint8_t> columns;
        std::optional<bool> landscape;
    };

    [[nodiscard]] bool phoneLike(const CropBox& box) const noexcept;
    [[nodiscard]] std::optional<bool> heldWay(const std::optional<CropBox>& instant) const;
    [[nodiscard]] std::optional<CropBox> candidateOf(const std::optional<CropBox>& instant,
                                                     const std::optional<CropBox>& held, bool& turned) const;

    CropOptions m_options;
    std::deque<Peaks> m_history;
    int m_width = 0;
    int m_height = 0;
    std::optional<CropBox> m_pending;
    Clock::time_point m_pendingSince;
    bool m_pendingTurned = false;
    std::optional<CropBox> m_accepted;
};

void peaksOf(const LumaPlane& plane, std::span<std::uint8_t> rows, std::span<std::uint8_t> columns);
[[nodiscard]] std::optional<CropBox> contentBox(std::span<const std::uint8_t> rows,
                                                std::span<const std::uint8_t> columns, int threshold);
[[nodiscard]] PixelBox pixelBoxOf(const CropBox& box, int width, int height) noexcept;
[[nodiscard]] bool landscapeIn(const CropBox& box, int width, int height) noexcept;
[[nodiscard]] int rotationOf(const CropBox& box, int width, int height, bool turnedRight) noexcept;

struct ContentRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

[[nodiscard]] std::optional<std::array<std::uint16_t, 2>> pictureFixed(const ContentRect& content, double x,
                                                                        double y) noexcept;
[[nodiscard]] std::array<double, 2> frameOf(const CropBox& box, double x, double y) noexcept;

}
