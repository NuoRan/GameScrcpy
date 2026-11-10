#pragma once

#include "gs/vision/Image.h"
#include "gs/vision/Simd.h"

#include <gs/base/Expected.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gs::vision {

inline constexpr int kMaxTemplateSide = 4096;
inline constexpr std::int64_t kMaxTemplatePixels = std::int64_t{1} << 22;
inline constexpr int kMaxPyramidLevel = 5;
inline constexpr double kColorRange = 128.0;

enum class TemplateError : std::uint8_t {
    Empty,
    TooLarge,
    MaskSize,
    Transparent,
    ColorSize,
};

struct TemplateLevel {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::int16_t> pixels;
    std::vector<Rect> opaque;
    std::int64_t count = 0;
    std::int64_t sum = 0;
    std::int64_t spread = 0;
};

class Template
{
public:
    Template() noexcept = default;

    [[nodiscard]] static Expected<Template, TemplateError> make(ImageView pixels, ImageView mask = {},
                                                                ColorView color = {});

    [[nodiscard]] bool empty() const noexcept { return m_levels.empty(); }
    [[nodiscard]] int width() const noexcept { return empty() ? 0 : m_levels.front().width; }
    [[nodiscard]] int height() const noexcept { return empty() ? 0 : m_levels.front().height; }
    [[nodiscard]] bool masked() const noexcept { return m_masked; }
    [[nodiscard]] bool flat() const noexcept { return empty() || m_levels.front().spread == 0; }
    [[nodiscard]] bool colored() const noexcept { return !m_color.empty(); }
    [[nodiscard]] ColorView color() const noexcept { return m_color.view(); }
    [[nodiscard]] int levelCount() const noexcept { return static_cast<int>(m_levels.size()); }
    [[nodiscard]] const TemplateLevel& level(int index) const noexcept
    {
        return m_levels[static_cast<std::size_t>(index)];
    }

private:
    std::vector<TemplateLevel> m_levels;
    ColorImage m_color;
    bool m_masked = false;
};

struct MatchOptions {
    Rect roi;
    double threshold = 0.8;
    int maxResults = 16;
    double maxOverlap = 0.3;
    int maxLevel = kMaxPyramidLevel;
    Simd simd = Simd::Automatic;
};

struct Match {
    Rect rect;
    double score = 0.0;
    int level = 0;

    friend bool operator==(const Match&, const Match&) = default;
};

struct ScoreMap {
    int width = 0;
    int height = 0;
    std::vector<double> scores;

    [[nodiscard]] double at(int x, int y) const noexcept
    {
        return scores[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

[[nodiscard]] std::optional<Match> findBest(ImageView image, const Template& pattern,
                                            const MatchOptions& options = {});

[[nodiscard]] std::vector<Match> findAll(ImageView image, const Template& pattern, const MatchOptions& options = {});

[[nodiscard]] double colorLikeness(ColorView image, const Template& pattern, int x, int y) noexcept;

[[nodiscard]] std::optional<Match> findBest(ImageView image, ColorView color, const Template& pattern,
                                            const MatchOptions& options = {});

[[nodiscard]] std::vector<Match> findAll(ImageView image, ColorView color, const Template& pattern,
                                         const MatchOptions& options = {});

[[nodiscard]] ScoreMap scoreMap(ImageView image, const Template& pattern, Simd simd = Simd::Automatic);

}
