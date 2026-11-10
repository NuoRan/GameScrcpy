#include "gs/record/RecordedVision.h"

#include "Pictures.h"
#include "gs/record/MediaFile.h"

#include <gs/media/FrameSnapshot.h>
#include <gs/media/VideoDecoder.h>
#include <gs/script/Resources.h>
#include <gs/script/TextSearch.h>
#include <gs/vision/Color.h>
#include <gs/vision/Glance.h>
#include <gs/vision/Matcher.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace gs::record {

namespace {

constexpr std::size_t kMostPatterns = 64;

std::filesystem::path pathOf(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

struct LatestFrame final : media::FrameSink {
    void onFrame(media::VideoFrame frame) override
    {
        latest = std::move(frame);
        ++count;
    }

    media::VideoFrame latest;
    std::uint64_t count = 0;
};

std::string labelOf(const script::FindQuery& query)
{
    return query.file == query.image ? query.image : std::format("{} ({})", query.image, query.file);
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

std::string foundJson(const vision::Rect& rect, double score, int width, int height)
{
    const double w = width;
    const double h = height;
    return std::format(R"({{"x":{},"y":{},"width":{},"height":{},"score":{}}})", (rect.x + rect.width / 2.0) / w,
                       (rect.y + rect.height / 2.0) / h, rect.width / w, rect.height / h, std::clamp(score, 0.0, 1.0));
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

std::shared_ptr<const vision::Template> templateOf(const Picture& picture, bool color, const std::string& label,
                                                   std::string& problem)
{
    if (picture.empty()) {
        problem = std::format("GS-API-016 {} is an empty picture", label);
        return nullptr;
    }
    const int width = picture.width;
    const int height = picture.height;
    const auto area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> luma(area);
    std::vector<std::uint8_t> mask(picture.alpha ? area : 0);
    std::vector<std::uint8_t> rgb(color ? 3 * area : 0);
    for (std::size_t at = 0; at < area; ++at) {
        const std::uint8_t* pixel = picture.rgba.data() + 4 * at;
        const double light = 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2];
        luma[at] = static_cast<std::uint8_t>(std::lround(16.0 + light * 219.0 / 255.0));
        if (picture.alpha) {
            mask[at] = pixel[3] >= 128 ? 255 : 0;
        }
        if (color) {
            std::memcpy(rgb.data() + 3 * at, pixel, 3);
        }
    }
    const vision::ImageView plane(luma.data(), width, height, width);
    const vision::ImageView shape =
        picture.alpha ? vision::ImageView(mask.data(), width, height, width) : vision::ImageView();
    const vision::ColorView colors =
        color ? vision::ColorView(rgb.data(), width, height, 3 * static_cast<std::ptrdiff_t>(width)) : vision::ColorView();
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

struct Loaded {
    Picture picture;
    std::string problem;
};

struct Pattern {
    std::shared_ptr<const vision::Template> pattern;
    std::string problem;
};

struct Captured {
    Picture picture;
    script::PictureSize source;
};

using PatternKey = std::tuple<std::string, bool, int, int>;

}

struct RecordedVision::State {
    State(std::filesystem::path logic, const std::filesystem::path& video, std::int64_t start)
        : folder(std::move(logic))
        , reader(video, protocol::MediaChannel::Video)
        , startPtsUs(start)
    {
    }

    std::filesystem::path folder;
    MediaReader reader;
    std::int64_t startPtsUs = 0;
    media::VideoDecoder decoder;
    LatestFrame sink;
    media::FrameSnapshots snapshots;
    std::uint64_t offered = 0;
    std::shared_ptr<const media::LumaSnapshot> grey;
    std::shared_ptr<const media::LumaSnapshot> colored;
    PictureReader pictures;
    std::map<std::string, Loaded> loaded;
    std::map<PatternKey, Pattern> patterns;
    std::map<std::uint64_t, Captured> captures;
    std::uint64_t lastCapture = 0;
    std::map<std::string, vision::Glance> glances;

    void advance(std::int64_t target)
    {
        for (;;) {
            const std::optional<protocol::MediaHeader> header = reader.peek();
            if (!header || (header->kind == protocol::MediaKind::Frame && sink.latest && header->ptsUs > target)) {
                return;
            }
            const std::optional<protocol::MediaPacket> packet = reader.next();
            if (!packet) {
                return;
            }
            static_cast<void>(decoder.push(*packet, sink));
        }
    }

    std::shared_ptr<const media::LumaSnapshot> shot(Microseconds at, bool color)
    {
        advance(startPtsUs + at.count());
        if (!sink.latest) {
            return nullptr;
        }
        if (offered != sink.count) {
            offered = sink.count;
            grey.reset();
            colored.reset();
            snapshots.offer(sink.latest);
        }
        if (colored != nullptr) {
            return colored;
        }
        if (!color && grey != nullptr) {
            return grey;
        }
        std::shared_ptr<const media::LumaSnapshot> made = snapshots.capture(color);
        (color ? colored : grey) = made;
        return made;
    }

    const Picture* pictureFor(const script::FindQuery& query, std::string& problem)
    {
        auto found = loaded.find(query.file);
        if (found == loaded.end()) {
            if (loaded.size() >= kMostPatterns) {
                loaded.clear();
            }
            Loaded made;
            const std::string label = labelOf(query);
            switch (pictures.read(folder / pathOf(query.file), made.picture)) {
            case PictureProblem::None:
                break;
            case PictureProblem::Missing:
                made.problem =
                    std::format("GS-API-016 {} cannot be read: there is no such file in the profile folder", label);
                break;
            case PictureProblem::TooLarge:
                made.problem =
                    std::format("GS-API-016 {} is larger than 4096 pixels on a side or 4194304 pixels", label);
                break;
            case PictureProblem::Unreadable:
                made.problem = std::format("GS-API-016 {} cannot be read as a picture", label);
                break;
            }
            found = loaded.emplace(query.file, std::move(made)).first;
        }
        if (!found->second.problem.empty()) {
            problem = found->second.problem;
            return nullptr;
        }
        return &found->second.picture;
    }

    std::shared_ptr<const vision::Template> patternFor(const script::FindQuery& query, const Picture& picture,
                                                       int width, int height, std::string& problem)
    {
        int targetWidth = picture.width;
        int targetHeight = picture.height;
        if (query.source.width > 0 && query.source.height > 0 && width > 0 && height > 0) {
            const double factor =
                static_cast<double>(std::max(width, height)) / std::max(query.source.width, query.source.height);
            targetWidth = std::max(1, static_cast<int>(std::lround(picture.width * factor)));
            targetHeight = std::max(1, static_cast<int>(std::lround(picture.height * factor)));
        }
        const bool color = query.match == script::MatchMode::Color;
        const PatternKey key{query.file, color, targetWidth, targetHeight};
        auto found = patterns.find(key);
        if (found == patterns.end()) {
            if (patterns.size() >= kMostPatterns) {
                patterns.clear();
            }
            Pattern made;
            const Picture scaled = pictures.scaled(picture, targetWidth, targetHeight);
            made.pattern = templateOf(scaled, color, labelOf(query), made.problem);
            found = patterns.emplace(key, std::move(made)).first;
        }
        problem = found->second.problem;
        return found->second.pattern;
    }
};

RecordedVision::RecordedVision(std::filesystem::path folder, const std::filesystem::path& video,
                               std::int64_t videoStartPtsUs)
    : m_state(std::make_unique<State>(std::move(folder), video, videoStartPtsUs))
{
}

RecordedVision::~RecordedVision() = default;

bool RecordedVision::opened() const noexcept
{
    return m_state->reader.opened();
}

std::int64_t RecordedVision::shownPtsUs() const noexcept
{
    return m_state->sink.latest ? m_state->sink.latest.ptsUs() : -1;
}

script::VisionAnswer RecordedVision::find(Microseconds at, const script::FindQuery& query)
{
    State& state = *m_state;
    std::string problem;
    script::FindQuery asked = query;
    const Picture* picture = nullptr;
    if (query.capture != 0) {
        const auto kept = state.captures.find(query.capture);
        if (kept == state.captures.end()) {
            return {.problem = std::format("GS-API-037 {} is no longer kept: a script keeps its {} latest captured images",
                                           query.image, script::kMostCaptures)};
        }
        asked.file = query.image;
        asked.source = kept->second.source;
        picture = &kept->second.picture;
    } else {
        picture = state.pictureFor(query, problem);
    }
    if (picture == nullptr || state.patternFor(asked, *picture, 0, 0, problem) == nullptr) {
        return {.problem = problem};
    }
    const std::shared_ptr<const media::LumaSnapshot> shot = state.shot(at, asked.match == script::MatchMode::Color);
    if (shot == nullptr) {
        return {.json = asked.all ? "[]" : "null"};
    }
    const std::shared_ptr<const vision::Template> pattern =
        state.patternFor(asked, *picture, shot->width, shot->height, problem);
    if (pattern == nullptr) {
        return {.problem = problem};
    }
    const vision::Rect area = pixelArea(asked.region, shot->width, shot->height);
    const std::vector<vision::Match> found =
        findTemplate(*shot, *pattern, area, asked.threshold, asked.most, asked.all);
    if (!asked.all) {
        return {.json = found.empty() ? std::string("null")
                                      : foundJson(found.front().rect, found.front().score, shot->width, shot->height)};
    }
    std::string json = "[";
    for (const vision::Match& match : found) {
        if (json.size() > 1) {
            json += ',';
        }
        json += foundJson(match.rect, match.score, shot->width, shot->height);
    }
    json += ']';
    return {.json = std::move(json)};
}

script::VisionAnswer RecordedVision::findColor(Microseconds at, const script::ColorQuery& query)
{
    const std::shared_ptr<const media::LumaSnapshot> shot = m_state->shot(at, true);
    if (shot == nullptr || !shot->colored()) {
        return {.json = "null"};
    }
    const vision::Rect area = pixelArea(query.region, shot->width, shot->height);
    const std::vector<std::uint8_t> rgb =
        area.empty() ? std::vector<std::uint8_t>() : media::rgbOf(*shot, area.x, area.y, area.width, area.height);
    if (rgb.empty()) {
        return {.json = "null"};
    }
    std::optional<vision::ColorPatch> patch = vision::findColor(
        vision::ColorView(rgb.data(), area.width, area.height, 3 * static_cast<std::ptrdiff_t>(area.width)),
        {query.rgb.red, query.rgb.green, query.rgb.blue}, {.tolerance = query.tolerance});
    if (!patch) {
        return {.json = "null"};
    }
    patch->rect.x += area.x;
    patch->rect.y += area.y;
    return {.json = foundJson(patch->rect, patch->score, shot->width, shot->height)};
}

script::VisionAnswer RecordedVision::pickColor(Microseconds at, const script::PixelQuery& query)
{
    const std::shared_ptr<const media::LumaSnapshot> shot = m_state->shot(at, true);
    const std::optional<vision::Rgb> seen = shot != nullptr ? pixelAt(*shot, query.at) : std::nullopt;
    if (!seen) {
        return {.json = query.match ? "false" : "null"};
    }
    if (!query.match) {
        return {.json = "\"" + script::colorText({seen->red, seen->green, seen->blue}) + "\""};
    }
    const std::uint8_t channels[] = {seen->red, seen->green, seen->blue};
    const bool matches = vision::colorMatches(channels, {query.rgb.red, query.rgb.green, query.rgb.blue},
                                              vision::colorReach(query.tolerance));
    return {.json = matches ? "true" : "false"};
}

script::VisionAnswer RecordedVision::capture(Microseconds at, const script::CaptureQuery& query)
{
    State& state = *m_state;
    const std::shared_ptr<const media::LumaSnapshot> shot = state.shot(at, true);
    if (shot == nullptr || !shot->colored()) {
        return {.json = "null"};
    }
    const vision::Rect area = pixelArea(query.region, shot->width, shot->height);
    const std::vector<std::uint8_t> rgb =
        area.empty() ? std::vector<std::uint8_t>() : media::rgbOf(*shot, area.x, area.y, area.width, area.height);
    const auto count = static_cast<std::size_t>(area.width) * static_cast<std::size_t>(area.height);
    if (rgb.empty() || rgb.size() < 3 * count) {
        return {.json = "null"};
    }
    Captured made{.picture = {.width = area.width, .height = area.height, .alpha = false, .rgba = {}},
                  .source = {shot->width, shot->height}};
    made.picture.rgba.resize(4 * count);
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        std::memcpy(made.picture.rgba.data() + 4 * pixel, rgb.data() + 3 * pixel, 3);
        made.picture.rgba[4 * pixel + 3] = 255;
    }
    const std::uint64_t capture = ++state.lastCapture;
    state.captures.emplace(capture, std::move(made));
    while (state.captures.size() > script::kMostCaptures) {
        state.captures.erase(state.captures.begin());
    }
    const double w = shot->width;
    const double h = shot->height;
    return {.json = std::format(R"({{"capture":{},"x":{},"y":{},"width":{},"height":{},"pixelWidth":{},"pixelHeight":{}}})",
                                capture, area.x / w, area.y / h, area.width / w, area.height / h, area.width,
                                area.height)};
}

script::VisionAnswer RecordedVision::changed(Microseconds at, const script::ChangeQuery& query)
{
    State& state = *m_state;
    const std::shared_ptr<const media::LumaSnapshot> shot = state.shot(at, false);
    if (shot == nullptr) {
        return {.json = "false"};
    }
    const vision::Rect area = pixelArea(query.region, shot->width, shot->height);
    vision::Glance seen =
        area.empty()
            ? vision::Glance{}
            : vision::glanceOf(vision::ImageView(shot->luma.data(), shot->width, shot->height, shot->width).crop(area));
    bool moved = false;
    if (const auto found = state.glances.find(query.key); found != state.glances.end()) {
        moved = vision::changedShare(found->second, seen) > query.threshold;
        found->second = std::move(seen);
    } else {
        if (state.glances.size() >= script::kMostChangeLooks) {
            state.glances.clear();
        }
        state.glances.emplace(query.key, std::move(seen));
    }
    return {.json = moved ? "true" : "false"};
}

script::VisionAnswer RecordedVision::readText(Microseconds at, const script::TextQuery& query)
{
    const std::shared_ptr<const media::LumaSnapshot> shot = m_state->shot(at, false);
    if (shot == nullptr) {
        return {.json = "null"};
    }
    const vision::Rect area = pixelArea(query.region, shot->width, shot->height);
    return script::answerText(query,
                              {.plane = shot->luma.data(),
                               .stride = shot->width,
                               .left = area.x,
                               .top = area.y,
                               .width = area.width,
                               .height = area.height},
                              {shot->width, shot->height});
}

}
