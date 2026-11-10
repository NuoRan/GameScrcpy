#include "gs/vision/Matcher.h"

#include "gs/vision/IntegralImage.h"

#include "Bands.h"
#include "Kernels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace gs::vision {

namespace {

constexpr double kDenseBudget = 8.0 * 1024.0 * 1024.0;
constexpr int kRefineRadius = 1;
constexpr int kMaxClimbs = 4;
constexpr std::size_t kMaxCandidates = 64;
constexpr double kUnpruned = -2.0;
constexpr int kLeastScanRows = 40;
constexpr int kLeastRefineCandidates = 16;

enum class Mode : std::uint8_t { Best, All };

struct Candidate {
    int x = 0;
    int y = 0;
    double score = 0.0;
};

struct Workspace {
    std::vector<std::int16_t> plane;
    std::vector<std::int64_t> products;
    std::vector<std::uint32_t> columnSums;
    std::vector<std::uint32_t> columnSquares;
    std::vector<double> scores;
    std::vector<double> map;
    IntegralImage integral;
};

Workspace& workspace()
{
    thread_local Workspace shared;
    return shared;
}

class Pyramid
{
public:
    Pyramid(ImageView base, int levels, Simd simd)
        : m_base(base)
    {
        m_levels.reserve(static_cast<std::size_t>(levels));
        for (int level = 1; level <= levels; ++level) {
            m_levels.push_back(downsample(level == 1 ? base : m_levels.back().view(), simd));
        }
    }

    [[nodiscard]] ImageView level(int index) const noexcept
    {
        return index == 0 ? m_base : m_levels[static_cast<std::size_t>(index - 1)].view();
    }

private:
    ImageView m_base;
    std::vector<Image> m_levels;
};

constexpr auto ranksBefore = [](const Candidate& a, const Candidate& b) noexcept {
    if (a.score != b.score) {
        return a.score > b.score;
    }
    return a.y != b.y ? a.y < b.y : a.x < b.x;
};

void evaluate(ImageView image, const TemplateLevel& pattern, const Rect& positions, const detail::Kernels& kernels,
              Workspace& work)
{
    const Rect region{positions.x, positions.y, positions.width + pattern.width - 1,
                      positions.height + pattern.height - 1};
    const ImageView pixels = image.crop(region);
    const std::ptrdiff_t stride = detail::planeStride(positions.width, pattern.stride);
    const auto needed = static_cast<std::size_t>(stride) * static_cast<std::size_t>(region.height);
    if (work.plane.size() < needed) {
        work.plane.resize(needed);
    }
    for (int y = 0; y < region.height; ++y) {
        const std::uint8_t* source = pixels.row(y);
        std::int16_t* target = work.plane.data() + y * stride;
        for (int x = 0; x < region.width; ++x) {
            target[x] = source[x];
        }
        std::fill(target + region.width, target + stride, std::int16_t{0});
    }
    const bool single = pattern.opaque.size() == 1;
    const Rect window = pattern.opaque.front();
    const auto span = static_cast<std::size_t>(positions.width + window.width - 1);
    if (single) {
        work.columnSums.resize(span);
        work.columnSquares.resize(span);
        kernels.accumulateColumns({.pixels = pixels.row(window.y) + window.x,
                                   .stride = pixels.stride(),
                                   .columns = static_cast<int>(span),
                                   .rows = window.height,
                                   .sums = work.columnSums.data(),
                                   .squares = work.columnSquares.data()});
    } else {
        work.integral.assign(pixels);
    }
    work.products.resize(static_cast<std::size_t>(positions.width));
    work.scores.resize(static_cast<std::size_t>(positions.width) * static_cast<std::size_t>(positions.height));
    const double patternSpread = static_cast<double>(pattern.spread);
    for (int y = 0; y < positions.height; ++y) {
        kernels.correlate({.image = work.plane.data() + y * stride,
                           .imageStride = stride,
                           .pattern = pattern.pixels.data(),
                           .patternWidth = pattern.width,
                           .patternStride = pattern.stride,
                           .patternHeight = pattern.height,
                           .columns = positions.width,
                           .rows = 1,
                           .sums = work.products.data(),
                           .sumStride = positions.width});
        std::int64_t rowSum = 0;
        std::int64_t rowSquares = 0;
        if (single) {
            if (y > 0) {
                const std::uint8_t* leaving = pixels.row(window.y + y - 1) + window.x;
                const std::uint8_t* entering = pixels.row(window.y + y - 1 + window.height) + window.x;
                for (std::size_t c = 0; c < span; ++c) {
                    const std::uint32_t out = leaving[c];
                    const std::uint32_t in = entering[c];
                    work.columnSums[c] += in - out;
                    work.columnSquares[c] += in * in - out * out;
                }
            }
            for (int c = 0; c < window.width; ++c) {
                rowSum += work.columnSums[static_cast<std::size_t>(c)];
                rowSquares += work.columnSquares[static_cast<std::size_t>(c)];
            }
        }
        double* scores = work.scores.data() + static_cast<std::ptrdiff_t>(y) * positions.width;
        for (int x = 0; x < positions.width; ++x) {
            std::int64_t sum = 0;
            std::int64_t squares = 0;
            if (single) {
                if (x > 0) {
                    const auto added = static_cast<std::size_t>(x + window.width - 1);
                    const auto removed = static_cast<std::size_t>(x - 1);
                    rowSum += std::int64_t{work.columnSums[added]} - std::int64_t{work.columnSums[removed]};
                    rowSquares += std::int64_t{work.columnSquares[added]} - std::int64_t{work.columnSquares[removed]};
                }
                sum = rowSum;
                squares = rowSquares;
            } else {
                for (const Rect& part : pattern.opaque) {
                    const Rect at{x + part.x, y + part.y, part.width, part.height};
                    sum += work.integral.sum(at);
                    squares += work.integral.squareSum(at);
                }
            }
            const std::int64_t spread = pattern.count * squares - sum * sum;
            double score = 0.0;
            if (pattern.spread > 0 && spread > 0) {
                const std::int64_t covariance =
                    pattern.count * work.products[static_cast<std::size_t>(x)] - pattern.sum * sum;
                score = std::clamp(static_cast<double>(covariance) /
                                       std::sqrt(patternSpread * static_cast<double>(spread)),
                                   -1.0, 1.0);
            }
            scores[x] = score;
        }
    }
}

double denseCost(const Rect& roi, const TemplateLevel& pattern, int level) noexcept
{
    const double columns = (roi.width >> level) - pattern.width + 1;
    const double rows = (roi.height >> level) - pattern.height + 1;
    return columns * rows * pattern.width * pattern.height;
}

int coarsestLevel(const Rect& roi, const Template& pattern, int maxLevel) noexcept
{
    const int top = std::clamp(maxLevel, 0, pattern.levelCount() - 1);
    int level = 0;
    while (level < top && denseCost(roi, pattern.level(level), level) > kDenseBudget) {
        ++level;
    }
    return level;
}

std::vector<Candidate> peaks(const std::vector<double>& scores, int width, int height)
{
    std::vector<Candidate> found;
    for (int y = 0; y < height; ++y) {
        const double* row = scores.data() + static_cast<std::ptrdiff_t>(y) * width;
        const double* above = y > 0 ? row - width : nullptr;
        const double* below = y + 1 < height ? row + width : nullptr;
        for (int x = 0; x < width; ++x) {
            const double score = row[x];
            const bool left = x > 0;
            const bool right = x + 1 < width;
            bool peak = (!left || score > row[x - 1]) && (!right || score >= row[x + 1]);
            if (peak && above != nullptr) {
                peak = (!left || score > above[x - 1]) && score > above[x] && (!right || score > above[x + 1]);
            }
            if (peak && below != nullptr) {
                peak = (!left || score >= below[x - 1]) && score >= below[x] && (!right || score >= below[x + 1]);
            }
            if (peak) {
                found.push_back({x, y, score});
            }
        }
    }
    return found;
}

Candidate climb(ImageView image, const TemplateLevel& pattern, int x, int y, double floor,
                const detail::Kernels& kernels, Workspace& work)
{
    const int lastX = image.width() - pattern.width;
    const int lastY = image.height() - pattern.height;
    Candidate best{std::clamp(x, 0, lastX), std::clamp(y, 0, lastY), 0.0};
    for (int step = 0; step <= kMaxClimbs; ++step) {
        const int left = std::max(best.x - kRefineRadius, 0);
        const int top = std::max(best.y - kRefineRadius, 0);
        const int right = std::min(best.x + kRefineRadius, lastX);
        const int bottom = std::min(best.y + kRefineRadius, lastY);
        const int columns = right - left + 1;
        evaluate(image, pattern, {left, top, columns, bottom - top + 1}, kernels, work);
        std::size_t chosen = 0;
        for (std::size_t index = 1; index < work.scores.size(); ++index) {
            if (work.scores[index] > work.scores[chosen]) {
                chosen = index;
            }
        }
        const auto width = static_cast<std::size_t>(columns);
        best = {left + static_cast<int>(chosen % width), top + static_cast<int>(chosen / width), work.scores[chosen]};
        const bool open = (best.x == left && left > 0) || (best.x == right && right < lastX) ||
                          (best.y == top && top > 0) || (best.y == bottom && bottom < lastY);
        if (!open || best.score < floor) {
            break;
        }
    }
    return best;
}

std::vector<Candidate> coarseCandidates(const std::vector<double>& scores, int width, int height, double floor,
                                        std::size_t least, std::size_t most)
{
    std::vector<Candidate> found = peaks(scores, width, height);
    const std::size_t ranked = std::min(found.size(), most);
    std::partial_sort(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(ranked), found.end(), ranksBefore);
    std::size_t keep = 0;
    while (keep < ranked && (keep < least || found[keep].score >= floor)) {
        ++keep;
    }
    found.resize(keep);
    return found;
}

std::vector<Match> search(ImageView image, const Template& pattern, const MatchOptions& options, Mode mode)
{
    if (image.empty() || pattern.empty() || pattern.flat()) {
        return {};
    }
    const Rect roi = options.roi.empty() ? image.bounds() : intersect(options.roi, image.bounds());
    if (roi.width < pattern.width() || roi.height < pattern.height()) {
        return {};
    }
    const std::size_t limit = mode == Mode::Best ? 1 : static_cast<std::size_t>(std::max(options.maxResults, 0));
    if (limit == 0) {
        return {};
    }
    const detail::Kernels& kernels = detail::kernels(options.simd);
    const int top = coarsestLevel(roi, pattern, options.maxLevel);
    const Pyramid pyramid(image.crop(roi), top, options.simd);
    Workspace& work = workspace();

    const ImageView coarse = pyramid.level(top);
    const TemplateLevel& coarsePattern = pattern.level(top);
    const int columns = coarse.width() - coarsePattern.width + 1;
    const int rows = coarse.height() - coarsePattern.height + 1;
    const int bands = detail::bandRows(rows, kLeastScanRows);
    std::vector<double>& map = bands <= 1 ? work.scores : work.map;
    if (bands <= 1) {
        evaluate(coarse, coarsePattern, {0, 0, columns, rows}, kernels, work);
    } else {
        map.resize(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
        detail::runBands(bands, [&](int band) {
            Workspace& bandWork = workspace();
            const int first = rows * band / bands;
            const int last = rows * (band + 1) / bands;
            evaluate(coarse, coarsePattern, {0, first, columns, last - first}, kernels, bandWork);
            std::copy_n(bandWork.scores.begin(), bandWork.scores.size(),
                        map.begin() + static_cast<std::ptrdiff_t>(first) * columns);
        });
    }

    std::vector<Candidate> candidates;
    if (top == 0 && mode == Mode::Best) {
        const auto best = std::ranges::max_element(map);
        const auto index = static_cast<std::size_t>(best - map.begin());
        const auto width = static_cast<std::size_t>(columns);
        candidates.push_back({static_cast<int>(index % width), static_cast<int>(index / width), *best});
    } else if (top == 0) {
        candidates = coarseCandidates(map, columns, rows, options.threshold, 0, map.size());
    } else {
        const std::size_t most = mode == Mode::Best ? kMaxCandidates : std::max(kMaxCandidates, 4 * limit);
        candidates = coarseCandidates(map, columns, rows, kUnpruned, most, most);
        for (int level = top - 1; level >= 0; --level) {
            const ImageView view = pyramid.level(level);
            const TemplateLevel& levelPattern = pattern.level(level);
            const double floor = level == 0 ? options.threshold : kUnpruned;
            const int ready = static_cast<int>(candidates.size());
            const int refine = detail::bandRows(ready, kLeastRefineCandidates);
            detail::runBands(refine, [&](int band) {
                Workspace& bandWork = workspace();
                const std::size_t total = candidates.size();
                const std::size_t first = total * static_cast<std::size_t>(band) / static_cast<std::size_t>(refine);
                const std::size_t last =
                    total * static_cast<std::size_t>(band + 1) / static_cast<std::size_t>(refine);
                for (std::size_t index = first; index < last; ++index) {
                    const Candidate start = candidates[index];
                    candidates[index] = climb(view, levelPattern, 2 * start.x, 2 * start.y, floor, kernels, bandWork);
                }
            });
            std::erase_if(candidates, [floor](const Candidate& candidate) { return candidate.score < floor; });
            std::ranges::sort(candidates, ranksBefore);
            const auto repeated = std::ranges::unique(
                candidates, [](const Candidate& a, const Candidate& b) { return a.x == b.x && a.y == b.y; });
            candidates.erase(repeated.begin(), repeated.end());
        }
    }

    const double maxOverlap = std::clamp(options.maxOverlap, 0.0, 1.0);
    std::vector<Match> matches;
    for (const Candidate& candidate : candidates) {
        if (candidate.score < options.threshold) {
            break;
        }
        const Rect rect{roi.x + candidate.x, roi.y + candidate.y, pattern.width(), pattern.height()};
        const bool suppressed = std::ranges::any_of(
            matches, [&rect, maxOverlap](const Match& kept) { return overlap(kept.rect, rect) > maxOverlap; });
        if (suppressed) {
            continue;
        }
        matches.push_back({rect, candidate.score, top});
        if (matches.size() == limit) {
            break;
        }
    }
    return matches;
}

}

std::optional<Match> findBest(ImageView image, const Template& pattern, const MatchOptions& options)
{
    const std::vector<Match> found = search(image, pattern, options, Mode::Best);
    if (found.empty()) {
        return std::nullopt;
    }
    return found.front();
}

std::vector<Match> findAll(ImageView image, const Template& pattern, const MatchOptions& options)
{
    return search(image, pattern, options, Mode::All);
}

double colorLikeness(ColorView image, const Template& pattern, int x, int y) noexcept
{
    if (image.empty() || !pattern.colored()) {
        return 0.0;
    }
    const TemplateLevel& base = pattern.level(0);
    if (x < 0 || y < 0 || x > image.width() - base.width || y > image.height() - base.height || base.count <= 0) {
        return 0.0;
    }
    const ColorView wanted = pattern.color();
    std::int64_t difference = 0;
    for (const Rect& part : base.opaque) {
        for (int row = part.y; row < part.y + part.height; ++row) {
            const std::uint8_t* expected = wanted.at(part.x, row);
            const std::uint8_t* seen = image.at(x + part.x, y + row);
            for (int index = 0; index < 3 * part.width; ++index) {
                difference += std::abs(static_cast<int>(expected[index]) - static_cast<int>(seen[index]));
            }
        }
    }
    const double mean = static_cast<double>(difference) / (3.0 * static_cast<double>(base.count));
    return std::clamp(1.0 - mean / kColorRange, 0.0, 1.0);
}

namespace {

std::vector<Match> colorSearch(ImageView image, ColorView color, const Template& pattern, const MatchOptions& options,
                               Mode mode)
{
    if (color.empty() || !pattern.colored()) {
        return search(image, pattern, options, mode);
    }
    if (color.width() != image.width() || color.height() != image.height()) {
        return {};
    }
    const std::size_t limit = mode == Mode::Best ? 1 : static_cast<std::size_t>(std::max(options.maxResults, 0));
    if (limit == 0) {
        return {};
    }
    MatchOptions wide = options;
    wide.maxResults = static_cast<int>(std::max(kMaxCandidates, 4 * limit));
    wide.maxOverlap = 1.0;
    std::vector<Match> candidates = search(image, pattern, wide, Mode::All);
    for (Match& candidate : candidates) {
        candidate.score =
            std::max(candidate.score, 0.0) * colorLikeness(color, pattern, candidate.rect.x, candidate.rect.y);
    }
    std::erase_if(candidates, [&options](const Match& candidate) { return candidate.score < options.threshold; });
    std::ranges::stable_sort(candidates, [](const Match& a, const Match& b) { return a.score > b.score; });
    const double maxOverlap = std::clamp(options.maxOverlap, 0.0, 1.0);
    std::vector<Match> matches;
    for (const Match& candidate : candidates) {
        const bool suppressed = std::ranges::any_of(matches, [&candidate, maxOverlap](const Match& kept) {
            return overlap(kept.rect, candidate.rect) > maxOverlap;
        });
        if (suppressed) {
            continue;
        }
        matches.push_back(candidate);
        if (matches.size() == limit) {
            break;
        }
    }
    return matches;
}

}

std::optional<Match> findBest(ImageView image, ColorView color, const Template& pattern, const MatchOptions& options)
{
    const std::vector<Match> found = colorSearch(image, color, pattern, options, Mode::Best);
    if (found.empty()) {
        return std::nullopt;
    }
    return found.front();
}

std::vector<Match> findAll(ImageView image, ColorView color, const Template& pattern, const MatchOptions& options)
{
    return colorSearch(image, color, pattern, options, Mode::All);
}

ScoreMap scoreMap(ImageView image, const Template& pattern, Simd simd)
{
    if (image.empty() || pattern.empty() || image.width() < pattern.width() || image.height() < pattern.height()) {
        return {};
    }
    const int columns = image.width() - pattern.width() + 1;
    const int rows = image.height() - pattern.height() + 1;
    Workspace work;
    evaluate(image, pattern.level(0), {0, 0, columns, rows}, detail::kernels(simd), work);
    return {columns, rows, std::move(work.scores)};
}

}
