#pragma once

#include "gs/vision/Simd.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gs::vision {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] std::int64_t area() const noexcept
    {
        return empty() ? 0 : static_cast<std::int64_t>(width) * height;
    }

    friend bool operator==(const Rect&, const Rect&) = default;
};

[[nodiscard]] Rect intersect(const Rect& a, const Rect& b) noexcept;

[[nodiscard]] double overlap(const Rect& a, const Rect& b) noexcept;

class ImageView
{
public:
    ImageView() noexcept = default;
    ImageView(const std::uint8_t* data, int width, int height, std::ptrdiff_t stride) noexcept
        : m_data(data)
        , m_width(width)
        , m_height(height)
        , m_stride(stride)
    {
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_data == nullptr || m_width <= 0 || m_height <= 0;
    }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] std::ptrdiff_t stride() const noexcept { return m_stride; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return m_data; }
    [[nodiscard]] const std::uint8_t* row(int y) const noexcept { return m_data + y * m_stride; }
    [[nodiscard]] std::uint8_t at(int x, int y) const noexcept { return row(y)[x]; }
    [[nodiscard]] Rect bounds() const noexcept { return {0, 0, m_width, m_height}; }

    [[nodiscard]] ImageView crop(const Rect& area) const noexcept;

private:
    const std::uint8_t* m_data = nullptr;
    int m_width = 0;
    int m_height = 0;
    std::ptrdiff_t m_stride = 0;
};

class Image
{
public:
    Image() noexcept = default;
    Image(int width, int height, std::uint8_t fill = 0);

    [[nodiscard]] static Image copyOf(ImageView source);

    [[nodiscard]] bool empty() const noexcept { return m_pixels.empty(); }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] std::uint8_t* data() noexcept { return m_pixels.data(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return m_pixels.data(); }
    [[nodiscard]] std::uint8_t* row(int y) noexcept
    {
        return m_pixels.data() + static_cast<std::ptrdiff_t>(y) * m_width;
    }
    [[nodiscard]] const std::uint8_t* row(int y) const noexcept
    {
        return m_pixels.data() + static_cast<std::ptrdiff_t>(y) * m_width;
    }
    [[nodiscard]] std::uint8_t& at(int x, int y) noexcept { return row(y)[x]; }
    [[nodiscard]] std::uint8_t at(int x, int y) const noexcept { return row(y)[x]; }
    [[nodiscard]] ImageView view() const noexcept { return {m_pixels.data(), m_width, m_height, m_width}; }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_pixels;
};

class ColorView
{
public:
    ColorView() noexcept = default;
    ColorView(const std::uint8_t* data, int width, int height, std::ptrdiff_t stride) noexcept
        : m_data(data)
        , m_width(width)
        , m_height(height)
        , m_stride(stride)
    {
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_data == nullptr || m_width <= 0 || m_height <= 0;
    }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] std::ptrdiff_t stride() const noexcept { return m_stride; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return m_data; }
    [[nodiscard]] const std::uint8_t* row(int y) const noexcept { return m_data + y * m_stride; }
    [[nodiscard]] const std::uint8_t* at(int x, int y) const noexcept { return row(y) + 3 * static_cast<std::ptrdiff_t>(x); }
    [[nodiscard]] Rect bounds() const noexcept { return {0, 0, m_width, m_height}; }

    [[nodiscard]] ColorView crop(const Rect& area) const noexcept;

private:
    const std::uint8_t* m_data = nullptr;
    int m_width = 0;
    int m_height = 0;
    std::ptrdiff_t m_stride = 0;
};

class ColorImage
{
public:
    ColorImage() noexcept = default;
    ColorImage(int width, int height);

    [[nodiscard]] static ColorImage copyOf(ColorView source);

    [[nodiscard]] bool empty() const noexcept { return m_pixels.empty(); }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] std::uint8_t* data() noexcept { return m_pixels.data(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return m_pixels.data(); }
    [[nodiscard]] std::uint8_t* row(int y) noexcept
    {
        return m_pixels.data() + 3 * static_cast<std::ptrdiff_t>(y) * m_width;
    }
    [[nodiscard]] std::uint8_t* at(int x, int y) noexcept { return row(y) + 3 * static_cast<std::ptrdiff_t>(x); }
    [[nodiscard]] ColorView view() const noexcept
    {
        return {m_pixels.data(), m_width, m_height, 3 * static_cast<std::ptrdiff_t>(m_width)};
    }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_pixels;
};

[[nodiscard]] Image downsample(ImageView source, Simd simd = Simd::Automatic);

}
