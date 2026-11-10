#include "gs/vision/Image.h"

#include "Bands.h"
#include "Kernels.h"

#include <algorithm>
#include <cstddef>

namespace gs::vision {

namespace {

constexpr int kLeastHalveRows = 128;

}

Rect intersect(const Rect& a, const Rect& b) noexcept
{
    if (a.empty() || b.empty()) {
        return {};
    }
    const std::int64_t left = std::max(a.x, b.x);
    const std::int64_t top = std::max(a.y, b.y);
    const std::int64_t right = std::min(std::int64_t{a.x} + a.width, std::int64_t{b.x} + b.width);
    const std::int64_t bottom = std::min(std::int64_t{a.y} + a.height, std::int64_t{b.y} + b.height);
    if (right <= left || bottom <= top) {
        return {};
    }
    return {static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
            static_cast<int>(bottom - top)};
}

double overlap(const Rect& a, const Rect& b) noexcept
{
    const std::int64_t shared = intersect(a, b).area();
    const std::int64_t together = a.area() + b.area() - shared;
    return together > 0 ? static_cast<double>(shared) / static_cast<double>(together) : 0.0;
}

ImageView ImageView::crop(const Rect& area) const noexcept
{
    const Rect inside = intersect(area, bounds());
    if (empty() || inside.empty()) {
        return {};
    }
    return {row(inside.y) + inside.x, inside.width, inside.height, m_stride};
}

Image::Image(int width, int height, std::uint8_t fill)
{
    if (width > 0 && height > 0) {
        m_width = width;
        m_height = height;
        m_pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill);
    }
}

Image Image::copyOf(ImageView source)
{
    if (source.empty()) {
        return {};
    }
    Image copy(source.width(), source.height());
    for (int y = 0; y < source.height(); ++y) {
        std::copy_n(source.row(y), source.width(), copy.row(y));
    }
    return copy;
}

Image downsample(ImageView source, Simd simd)
{
    if (source.empty()) {
        return {};
    }
    Image half(source.width() / 2, source.height() / 2);
    if (half.empty()) {
        return half;
    }
    const detail::Kernels& kernels = detail::kernels(simd);
    const int rows = half.height();
    const int bands = detail::bandRows(rows, kLeastHalveRows);
    detail::runBands(bands, [&](int band) {
        const int first = rows * band / bands;
        const int last = rows * (band + 1) / bands;
        for (int y = first; y < last; ++y) {
            kernels.halve(source.row(2 * y), source.row(2 * y + 1), half.row(y), half.width());
        }
    });
    return half;
}

ColorView ColorView::crop(const Rect& area) const noexcept
{
    const Rect inside = intersect(area, bounds());
    if (empty() || inside.empty()) {
        return {};
    }
    return {at(inside.x, inside.y), inside.width, inside.height, m_stride};
}

ColorImage::ColorImage(int width, int height)
{
    if (width > 0 && height > 0) {
        m_width = width;
        m_height = height;
        m_pixels.assign(std::size_t{3} * static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    }
}

ColorImage ColorImage::copyOf(ColorView source)
{
    if (source.empty()) {
        return {};
    }
    ColorImage copy(source.width(), source.height());
    for (int y = 0; y < source.height(); ++y) {
        std::copy_n(source.row(y), 3 * static_cast<std::ptrdiff_t>(source.width()), copy.row(y));
    }
    return copy;
}

}
