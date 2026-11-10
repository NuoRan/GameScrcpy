#include "VisionSearch.h"

#include <gs/script/Resources.h>

#include <QImageReader>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <utility>

namespace gs::app {

QImage readPicture(const QString& path, const std::string& label, std::string& problem)
{
    QImageReader reader(path);
    reader.setAutoTransform(false);
    if (const QSize size = reader.size(); size.isValid()
        && (size.width() > vision::kMaxTemplateSide || size.height() > vision::kMaxTemplateSide
            || std::int64_t{size.width()} * size.height() > vision::kMaxTemplatePixels)) {
        problem = std::format("GS-API-016 {} is larger than 4096 pixels on a side or 4194304 pixels", label);
        return {};
    }
    QImage picture;
    if (!reader.read(&picture) || picture.isNull()) {
        problem = std::format("GS-API-016 {} cannot be read as a picture", label);
        return {};
    }
    return picture;
}

QImage scaledPicture(const QImage& picture, script::PictureSize source, int width, int height)
{
    if (picture.isNull() || source.width <= 0 || source.height <= 0 || width <= 0 || height <= 0) {
        return picture;
    }
    const double factor = static_cast<double>(std::max(width, height)) / std::max(source.width, source.height);
    const QSize target(std::max(1, static_cast<int>(std::lround(picture.width() * factor))),
                       std::max(1, static_cast<int>(std::lround(picture.height() * factor))));
    if (target == picture.size()) {
        return picture;
    }
    return picture.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

std::shared_ptr<const vision::Template> templateOf(const QImage& picture, bool color, const std::string& label,
                                                   std::string& problem)
{
    if (picture.isNull()) {
        problem = std::format("GS-API-016 {} is an empty picture", label);
        return nullptr;
    }
    const bool alpha = picture.hasAlphaChannel();
    const QImage pixels = picture.convertToFormat(QImage::Format_RGBA8888);
    const int width = pixels.width();
    const int height = pixels.height();
    const auto area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> luma(area);
    std::vector<std::uint8_t> mask(alpha ? area : 0);
    std::vector<std::uint8_t> rgb(color ? 3 * area : 0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* line = pixels.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = line + 4 * static_cast<std::ptrdiff_t>(x);
            const double light = 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2];
            const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x;
            luma[at] = static_cast<std::uint8_t>(std::lround(16.0 + light * 219.0 / 255.0));
            if (alpha) {
                mask[at] = pixel[3] >= 128 ? 255 : 0;
            }
            if (color) {
                std::memcpy(rgb.data() + 3 * at, pixel, 3);
            }
        }
    }
    const vision::ImageView plane(luma.data(), width, height, width);
    const vision::ImageView shape = alpha ? vision::ImageView(mask.data(), width, height, width) : vision::ImageView();
    const vision::ColorView colors = color ? vision::ColorView(rgb.data(), width, height, 3 * static_cast<std::ptrdiff_t>(width))
                                           : vision::ColorView();
    auto made = vision::Template::make(plane, shape, colors);
    if (!made) {
        switch (made.error()) {
        case vision::TemplateError::Empty:
        case vision::TemplateError::MaskSize:
        case vision::TemplateError::ColorSize:
            problem = std::format("GS-API-016 {} is an empty picture", label);
            break;
        case vision::TemplateError::TooLarge:
            problem = std::format("GS-API-016 {} is larger than 4096 pixels on a side or 4194304 pixels", label);
            break;
        case vision::TemplateError::Transparent:
            problem = std::format("GS-API-016 {} is fully transparent", label);
            break;
        }
        return nullptr;
    }
    if (made->flat()) {
        problem = std::format("GS-API-016 {} has a single colour, so there is nothing to compare", label);
        return nullptr;
    }
    return std::make_shared<const vision::Template>(std::move(*made));
}

vision::Rect pixelArea(const script::Region& region, int width, int height)
{
    const auto edge = [](double ratio, int size, bool up) {
        const double at = ratio * size;
        return std::clamp(static_cast<int>(up ? std::ceil(at - 1e-6) : std::floor(at + 1e-6)), 0, size);
    };
    const int left = edge(region.x, width, false);
    const int top = edge(region.y, height, false);
    const int right = edge(region.x + region.width, width, true);
    const int bottom = edge(region.y + region.height, height, true);
    return {left, top, std::max(right - left, 0), std::max(bottom - top, 0)};
}

std::vector<vision::Match> findTemplate(const media::LumaSnapshot& shot, const vision::Template& pattern,
                                        const vision::Rect& area, double threshold, int most, bool all)
{
    if (area.empty() || shot.width <= 0 || shot.height <= 0) {
        return {};
    }
    const vision::ImageView image(shot.luma.data(), shot.width, shot.height, shot.width);
    const vision::MatchOptions options{.roi = area, .threshold = threshold, .maxResults = all ? most : 1};
    std::vector<std::uint8_t> rgb;
    if (pattern.colored() && shot.colored()) {
        rgb = media::rgbOf(shot, area.x, area.y, area.width, area.height);
    }
    if (rgb.empty()) {
        if (all) {
            return vision::findAll(image, pattern, options);
        }
        const std::optional<vision::Match> best = vision::findBest(image, pattern, options);
        return best ? std::vector<vision::Match>{*best} : std::vector<vision::Match>{};
    }
    const vision::ColorView colors(rgb.data(), area.width, area.height, 3 * static_cast<std::ptrdiff_t>(area.width));
    vision::MatchOptions inside = options;
    inside.roi = {};
    std::vector<vision::Match> found;
    if (all) {
        found = vision::findAll(image.crop(area), colors, pattern, inside);
    } else if (const std::optional<vision::Match> best = vision::findBest(image.crop(area), colors, pattern, inside)) {
        found.push_back(*best);
    }
    for (vision::Match& match : found) {
        match.rect.x += area.x;
        match.rect.y += area.y;
    }
    return found;
}

std::optional<vision::ColorPatch> findColorIn(const media::LumaSnapshot& shot, vision::Rgb color, double tolerance,
                                              const vision::Rect& area)
{
    if (area.empty() || !shot.colored()) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> rgb = media::rgbOf(shot, area.x, area.y, area.width, area.height);
    if (rgb.empty()) {
        return std::nullopt;
    }
    std::optional<vision::ColorPatch> patch =
        vision::findColor(vision::ColorView(rgb.data(), area.width, area.height, 3 * static_cast<std::ptrdiff_t>(area.width)),
                          color, {.tolerance = tolerance});
    if (patch) {
        patch->rect.x += area.x;
        patch->rect.y += area.y;
    }
    return patch;
}

std::optional<vision::Rgb> pixelAt(const media::LumaSnapshot& shot, script::Point at)
{
    if (!shot.colored() || shot.width <= 0 || shot.height <= 0) {
        return std::nullopt;
    }
    const auto cell = [](double ratio, int size) {
        return std::clamp(static_cast<int>(std::floor(ratio * size + 1e-6)), 0, size - 1);
    };
    const std::vector<std::uint8_t> rgb = media::rgbOf(shot, cell(at.x, shot.width), cell(at.y, shot.height), 1, 1);
    if (rgb.size() < 3) {
        return std::nullopt;
    }
    return vision::Rgb{rgb[0], rgb[1], rgb[2]};
}

std::string pixelJson(const media::LumaSnapshot& shot, const script::PixelQuery& query)
{
    const std::optional<vision::Rgb> seen = pixelAt(shot, query.at);
    if (!seen) {
        return query.match ? "false" : "null";
    }
    if (!query.match) {
        return "\"" + script::colorText({seen->red, seen->green, seen->blue}) + "\"";
    }
    const std::uint8_t channels[] = {seen->red, seen->green, seen->blue};
    const bool matches = vision::colorMatches(channels, {query.rgb.red, query.rgb.green, query.rgb.blue},
                                              vision::colorReach(query.tolerance));
    return matches ? "true" : "false";
}

std::string foundJson(const vision::Rect& rect, double score, int width, int height)
{
    const double w = width;
    const double h = height;
    return std::format(R"({{"x":{},"y":{},"width":{},"height":{},"score":{}}})", (rect.x + rect.width / 2.0) / w,
                       (rect.y + rect.height / 2.0) / h, rect.width / w, rect.height / h, std::clamp(score, 0.0, 1.0));
}

QImage pictureOf(const media::LumaSnapshot& shot)
{
    const std::vector<std::uint8_t> rgb = media::rgbOf(shot, 0, 0, shot.width, shot.height);
    if (rgb.empty()) {
        return {};
    }
    QImage picture(shot.width, shot.height, QImage::Format_RGB888);
    const auto row = 3 * static_cast<std::size_t>(shot.width);
    for (int y = 0; y < shot.height; ++y) {
        std::memcpy(picture.scanLine(y), rgb.data() + static_cast<std::size_t>(y) * row, row);
    }
    return picture;
}

std::string templateFileFor(const QString& name, const std::vector<script::TemplateResource>& templates,
                            const std::string& keep, const std::function<bool(const std::string&)>& foreign)
{
    QString stem;
    for (const QChar c : name) {
        stem += c.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(c) ? QChar(u'_') : c;
    }
    while (!stem.isEmpty() && (stem.endsWith(u'.') || stem.endsWith(u' '))) {
        stem.chop(1);
    }
    while (!stem.isEmpty() && (stem.startsWith(u'.') || stem.startsWith(u' '))) {
        stem.remove(0, 1);
    }
    for (int attempt = 1;; ++attempt) {
        const QString base = stem.isEmpty() || attempt > 1000 ? QStringLiteral("template") : stem;
        const QString candidate =
            (attempt == 1 ? base : QStringLiteral("%1 (%2)").arg(base).arg(attempt)) + QStringLiteral(".png");
        const std::string file = candidate.toStdString();
        if (!script::resourceFile(file)) {
            stem.clear();
            continue;
        }
        if (!keep.empty() && QString::compare(candidate, QString::fromStdString(keep), Qt::CaseInsensitive) == 0) {
            return keep;
        }
        const bool taken = std::ranges::any_of(templates, [&candidate](const script::TemplateResource& entry) {
            return QString::compare(QString::fromStdString(entry.file), candidate, Qt::CaseInsensitive) == 0;
        });
        if (!taken && !(foreign && foreign(file))) {
            return file;
        }
    }
}

}
