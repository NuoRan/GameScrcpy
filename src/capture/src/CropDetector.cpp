#include "gs/capture/CropDetector.h"

#include <algorithm>
#include <cmath>

namespace gs::capture {

namespace {

constexpr double kFixedScale = 65535.0;

}

bool CropBox::whole(double slack) const noexcept
{
    return x <= slack && y <= slack && x + width >= 1.0 - slack && y + height >= 1.0 - slack;
}

bool CropBox::contains(const CropBox& other, double slack) const noexcept
{
    return other.x >= x - slack && other.y >= y - slack && other.x + other.width <= x + width + slack &&
           other.y + other.height <= y + height + slack;
}

bool CropBox::closeTo(const CropBox& other, double slack) const noexcept
{
    return std::abs(x - other.x) <= slack && std::abs(y - other.y) <= slack &&
           std::abs(x + width - other.x - other.width) <= slack &&
           std::abs(y + height - other.y - other.height) <= slack;
}

CropDetector::CropDetector(CropOptions options) : m_options(options) {}

void CropDetector::reset()
{
    m_history.clear();
    m_width = 0;
    m_height = 0;
    m_pending.reset();
    m_pendingTurned = false;
    m_accepted.reset();
}

bool CropDetector::phoneLike(const CropBox& box) const noexcept
{
    const double pixelWidth = box.width * m_width;
    const double pixelHeight = box.height * m_height;
    if (!(pixelWidth > 0.0) || !(pixelHeight > 0.0)) {
        return false;
    }
    const double aspect = std::max(pixelWidth, pixelHeight) / std::min(pixelWidth, pixelHeight);
    const bool fills = box.width >= m_options.fill || box.height >= m_options.fill;
    return fills && aspect >= m_options.narrowest && aspect <= m_options.widest;
}

std::optional<bool> CropDetector::heldWay(const std::optional<CropBox>& instant) const
{
    if (m_accepted) {
        return landscapeIn(*m_accepted, m_width, m_height);
    }
    if (instant) {
        return landscapeIn(*instant, m_width, m_height);
    }
    for (auto kept = m_history.rbegin(); kept != m_history.rend(); ++kept) {
        if (kept->landscape) {
            return kept->landscape;
        }
    }
    return std::nullopt;
}

std::optional<CropBox> CropDetector::candidateOf(const std::optional<CropBox>& instant,
                                                 const std::optional<CropBox>& held, bool& turned) const
{
    turned = false;
    if (!m_accepted) {
        if (held && phoneLike(*held)) {
            return held;
        }
        return std::nullopt;
    }
    const double slack = m_options.slack;
    const bool acceptedLandscape = landscapeIn(*m_accepted, m_width, m_height);
    if (instant && phoneLike(*instant)) {
        const bool otherWay = landscapeIn(*instant, m_width, m_height) != acceptedLandscape;
        if (otherWay || !m_accepted->contains(*instant, slack)) {
            turned = otherWay;
            return instant;
        }
    }
    if (held && phoneLike(*held) && !m_accepted->contains(*held, slack) &&
        landscapeIn(*held, m_width, m_height) == acceptedLandscape) {
        return held;
    }
    return std::nullopt;
}

bool CropDetector::feed(const LumaPlane& plane, Clock::time_point at)
{
    if (plane.pixels == nullptr || plane.width < 2 || plane.height < 2 || plane.pitch < plane.width) {
        return false;
    }
    bool changed = false;
    if (plane.width != m_width || plane.height != m_height) {
        changed = m_accepted.has_value();
        reset();
        m_width = plane.width;
        m_height = plane.height;
    }
    Peaks peaks{.at = at,
                .rows = std::vector<std::uint8_t>(static_cast<std::size_t>(plane.height), 0),
                .columns = std::vector<std::uint8_t>(static_cast<std::size_t>(plane.width), 0)};
    peaksOf(plane, peaks.rows, peaks.columns);
    const std::optional<CropBox> instant = contentBox(peaks.rows, peaks.columns, m_options.threshold);
    if (instant) {
        peaks.landscape = landscapeIn(*instant, m_width, m_height);
    }
    m_history.push_back(std::move(peaks));
    while (m_history.size() > 1 && at - m_history.front().at > m_options.hold) {
        m_history.pop_front();
    }
    const std::optional<bool> way = heldWay(instant);
    std::vector<std::uint8_t> rows(static_cast<std::size_t>(m_height), 0);
    std::vector<std::uint8_t> columns(static_cast<std::size_t>(m_width), 0);
    for (const Peaks& kept : m_history) {
        if (way && kept.landscape && *kept.landscape != *way) {
            continue;
        }
        std::ranges::transform(rows, kept.rows, rows.begin(), [](std::uint8_t a, std::uint8_t b) { return std::max(a, b); });
        std::ranges::transform(columns, kept.columns, columns.begin(),
                               [](std::uint8_t a, std::uint8_t b) { return std::max(a, b); });
    }
    const std::optional<CropBox> held = contentBox(rows, columns, m_options.threshold);
    bool turned = false;
    const std::optional<CropBox> candidate = candidateOf(instant, held, turned);
    if (!candidate) {
        m_pending.reset();
        return changed;
    }
    if (!m_pending || !m_pending->closeTo(*candidate, m_options.slack)) {
        m_pending = candidate;
        m_pendingSince = at;
        m_pendingTurned = turned;
        return changed;
    }
    if (at - m_pendingSince < m_options.stable) {
        return changed;
    }
    m_accepted = candidate;
    if (m_pendingTurned) {
        Peaks latest = std::move(m_history.back());
        m_history.clear();
        m_history.push_back(std::move(latest));
    }
    m_pending.reset();
    m_pendingTurned = false;
    return true;
}

void peaksOf(const LumaPlane& plane, std::span<std::uint8_t> rows, std::span<std::uint8_t> columns)
{
    std::ranges::fill(rows, std::uint8_t{0});
    std::ranges::fill(columns, std::uint8_t{0});
    const int width = std::min(plane.width, static_cast<int>(columns.size()));
    const int height = std::min(plane.height, static_cast<int>(rows.size()));
    if (plane.pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = plane.pixels + static_cast<std::ptrdiff_t>(y) * plane.pitch;
        const std::uint8_t* below =
            y + 1 < height ? row + plane.pitch : nullptr;
        std::uint8_t peak = width == 1 ? row[0] : std::uint8_t{0};
        for (int x = 0; x + 1 < width; ++x) {
            peak = std::max(peak, std::min(row[x], row[x + 1]));
        }
        rows[static_cast<std::size_t>(y)] = peak;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t lone = height == 1 ? row[x] : std::uint8_t{0};
            const std::uint8_t value = below != nullptr ? std::min(row[x], below[x]) : lone;
            std::uint8_t& column = columns[static_cast<std::size_t>(x)];
            column = std::max(column, value);
        }
    }
}

std::optional<CropBox> contentBox(std::span<const std::uint8_t> rows, std::span<const std::uint8_t> columns,
                                  int threshold)
{
    const auto bright = [threshold](std::uint8_t value) { return static_cast<int>(value) > threshold; };
    const auto top = std::ranges::find_if(rows, bright);
    const auto left = std::ranges::find_if(columns, bright);
    if (top == rows.end() || left == columns.end()) {
        return std::nullopt;
    }
    const auto bottom = std::ranges::find_if(rows.rbegin(), rows.rend(), bright);
    const auto right = std::ranges::find_if(columns.rbegin(), columns.rend(), bright);
    const auto first = static_cast<double>(top - rows.begin());
    const auto last = static_cast<double>(rows.rend() - bottom);
    const auto start = static_cast<double>(left - columns.begin());
    const auto end = static_cast<double>(columns.rend() - right);
    const auto height = static_cast<double>(rows.size());
    const auto width = static_cast<double>(columns.size());
    return CropBox{.x = start / width, .y = first / height, .width = (end - start) / width,
                   .height = (last - first) / height};
}

PixelBox pixelBoxOf(const CropBox& box, int width, int height) noexcept
{
    if (width < 2 || height < 2) {
        return {0, 0, std::max(width, 0), std::max(height, 0)};
    }
    const auto even = [](int value) { return value & ~1; };
    const int evenWidth = even(width);
    const int evenHeight = even(height);
    const int left = std::clamp(even(static_cast<int>(std::floor(box.x * width + 1e-6))), 0, evenWidth - 2);
    const int top = std::clamp(even(static_cast<int>(std::floor(box.y * height + 1e-6))), 0, evenHeight - 2);
    const int right =
        std::clamp(even(static_cast<int>(std::ceil((box.x + box.width) * width - 1e-6)) + 1), left + 2, evenWidth);
    const int bottom =
        std::clamp(even(static_cast<int>(std::ceil((box.y + box.height) * height - 1e-6)) + 1), top + 2, evenHeight);
    return {left, top, right - left, bottom - top};
}

bool landscapeIn(const CropBox& box, int width, int height) noexcept
{
    return box.width * width > box.height * height;
}

int rotationOf(const CropBox& box, int width, int height, bool turnedRight) noexcept
{
    if (!landscapeIn(box, width, height)) {
        return 0;
    }
    return turnedRight ? 3 : 1;
}

std::optional<std::array<std::uint16_t, 2>> pictureFixed(const ContentRect& content, double x, double y) noexcept
{
    if (!(content.width > 0.0) || !(content.height > 0.0) || x < content.x || y < content.y ||
        x > content.x + content.width || y > content.y + content.height) {
        return std::nullopt;
    }
    const double u = std::clamp((x - content.x) / content.width, 0.0, 1.0);
    const double v = std::clamp((y - content.y) / content.height, 0.0, 1.0);
    return std::array<std::uint16_t, 2>{static_cast<std::uint16_t>(std::lround(u * kFixedScale)),
                                        static_cast<std::uint16_t>(std::lround(v * kFixedScale))};
}

std::array<double, 2> frameOf(const CropBox& box, double x, double y) noexcept
{
    return {box.x + x * box.width, box.y + y * box.height};
}

}
