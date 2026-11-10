#include "gs/vision/Matcher.h"

#include "Kernels.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace gs::vision {

namespace {

constexpr int kMinLevelSide = 6;
constexpr std::int64_t kMinLevelPixels = 144;

std::vector<Rect> opaqueRects(const Image& mask)
{
    std::vector<Rect> done;
    std::vector<Rect> open;
    std::vector<Rect> next;
    for (int y = 0; y < mask.height(); ++y) {
        const std::uint8_t* row = mask.row(y);
        next.clear();
        std::size_t index = 0;
        int x = 0;
        while (x < mask.width()) {
            if (row[x] == 0) {
                ++x;
                continue;
            }
            int end = x;
            while (end < mask.width() && row[end] != 0) {
                ++end;
            }
            while (index < open.size() && open[index].x < x) {
                done.push_back(open[index++]);
            }
            if (index < open.size() && open[index].x == x && open[index].width == end - x) {
                Rect grown = open[index++];
                ++grown.height;
                next.push_back(grown);
            } else {
                next.push_back({x, y, end - x, 1});
            }
            x = end;
        }
        while (index < open.size()) {
            done.push_back(open[index++]);
        }
        open.swap(next);
    }
    done.insert(done.end(), open.begin(), open.end());
    std::ranges::sort(done, [](const Rect& a, const Rect& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    return done;
}

TemplateLevel describe(const Image& pixels, const Image* mask)
{
    TemplateLevel level;
    level.width = pixels.width();
    level.height = pixels.height();
    level.stride = detail::paddedWidth(level.width);
    level.pixels.assign(static_cast<std::size_t>(level.stride) * static_cast<std::size_t>(level.height), 0);
    std::int64_t squares = 0;
    for (int y = 0; y < level.height; ++y) {
        std::int16_t* target = level.pixels.data() + static_cast<std::ptrdiff_t>(y) * level.stride;
        for (int x = 0; x < level.width; ++x) {
            if (mask != nullptr && mask->at(x, y) == 0) {
                continue;
            }
            const std::int64_t value = pixels.at(x, y);
            target[x] = static_cast<std::int16_t>(value);
            ++level.count;
            level.sum += value;
            squares += value * value;
        }
    }
    level.opaque = mask != nullptr ? opaqueRects(*mask) : std::vector<Rect>{{0, 0, level.width, level.height}};
    level.spread = level.count * squares - level.sum * level.sum;
    return level;
}

Image shrinkMask(const Image& mask)
{
    Image half(mask.width() / 2, mask.height() / 2);
    for (int y = 0; y < half.height(); ++y) {
        const std::uint8_t* upper = mask.row(2 * y);
        const std::uint8_t* lower = mask.row(2 * y + 1);
        for (int x = 0; x < half.width(); ++x) {
            const bool opaque =
                upper[2 * x] != 0 && upper[2 * x + 1] != 0 && lower[2 * x] != 0 && lower[2 * x + 1] != 0;
            half.at(x, y) = static_cast<std::uint8_t>(opaque ? 255 : 0);
        }
    }
    return half;
}

}

Expected<Template, TemplateError> Template::make(ImageView pixels, ImageView mask, ColorView color)
{
    if (pixels.empty()) {
        return makeUnexpected(TemplateError::Empty);
    }
    if (pixels.width() > kMaxTemplateSide || pixels.height() > kMaxTemplateSide ||
        std::int64_t{pixels.width()} * pixels.height() > kMaxTemplatePixels) {
        return makeUnexpected(TemplateError::TooLarge);
    }
    if (!color.empty() && (color.width() != pixels.width() || color.height() != pixels.height())) {
        return makeUnexpected(TemplateError::ColorSize);
    }
    Image shape;
    if (!mask.empty()) {
        if (mask.width() != pixels.width() || mask.height() != pixels.height()) {
            return makeUnexpected(TemplateError::MaskSize);
        }
        shape = Image(mask.width(), mask.height());
        bool opaque = false;
        bool transparent = false;
        for (int y = 0; y < mask.height(); ++y) {
            for (int x = 0; x < mask.width(); ++x) {
                const bool covered = mask.at(x, y) != 0;
                shape.at(x, y) = static_cast<std::uint8_t>(covered ? 255 : 0);
                opaque = opaque || covered;
                transparent = transparent || !covered;
            }
        }
        if (!opaque) {
            return makeUnexpected(TemplateError::Transparent);
        }
        if (!transparent) {
            shape = Image();
        }
    }

    Template made;
    made.m_masked = !shape.empty();
    made.m_color = ColorImage::copyOf(color);
    Image level = Image::copyOf(pixels);
    made.m_levels.push_back(describe(level, made.m_masked ? &shape : nullptr));
    while (made.levelCount() <= kMaxPyramidLevel) {
        const int width = level.width() / 2;
        const int height = level.height() / 2;
        if (width < kMinLevelSide || height < kMinLevelSide || std::int64_t{width} * height < kMinLevelPixels) {
            break;
        }
        Image smaller = downsample(level.view());
        Image smallerShape = made.m_masked ? shrinkMask(shape) : Image();
        TemplateLevel next = describe(smaller, made.m_masked ? &smallerShape : nullptr);
        if (next.count < kMinLevelPixels || next.spread == 0) {
            break;
        }
        made.m_levels.push_back(std::move(next));
        level = std::move(smaller);
        shape = std::move(smallerShape);
    }
    return made;
}

}
