#pragma once

#include <gs/media/LumaSampler.h>
#include <gs/protocol/Messages.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace gs::app {

struct LatencyResult {
    int samples = 0;
    double medianMs = 0;
    double lowMs = 0;
    double highMs = 0;
};

struct LatencyProgress {
    bool running = false;
    int measured = 0;
    int target = 0;
    int missed = 0;
    bool touchesRefused = false;
    std::optional<LatencyResult> result;
};

struct LatencyMeterOptions {
    int target = 10;
    int fewest = 3;
    int mostWatched = 4;
    int mostMissed = 15;
    std::int64_t detectWithinUs = 1'000'000;
    std::int64_t settleUs = 400'000;
    std::int64_t stillGapUs = 60'000;
    double minimumChange = 12;
    double ringFactor = 3;
    double ringSlack = 4;
    double coreDp = 3;
    double ringFromDp = 20;
    double ringToDp = 28;
    double moveDp = 4;
};

class LatencyMeter
{
public:
    LatencyMeter();
    explicit LatencyMeter(LatencyMeterOptions options);

    void touched(const protocol::TouchFrame& frame, std::int64_t nowUs);

    struct Plan {
        std::vector<std::uint32_t> fresh;
        std::vector<media::LumaSpot> freshSpots;
        std::vector<std::uint32_t> watched;
        std::vector<media::LumaSpot> watchedSpots;

        [[nodiscard]] bool empty() const noexcept { return fresh.empty() && watched.empty(); }
    };

    [[nodiscard]] Plan plan(int frameWidth, int frameHeight, double pixelsPerDp, std::int64_t nowUs);
    void observe(const Plan& plan, std::span<const media::LumaReading> fresh,
                 std::span<const media::LumaReading> watched, std::int64_t ptsUs, std::int64_t nowUs);
    void shown(std::int64_t ptsUs, std::int64_t shownUs);
    void finish();

    [[nodiscard]] LatencyProgress progress() const;

private:
    enum class Stage : std::uint8_t { Fresh, Watching, Candidate, Seen };

    struct Point {
        double u = 0;
        double v = 0;
    };

    struct Probe {
        std::uint32_t id = 0;
        std::uint8_t pointerId = 0;
        Point at;
        std::int64_t pressUs = 0;
        bool held = true;
        Stage stage = Stage::Fresh;
        media::LumaReading base;
        std::int64_t candidatePts = 0;
        std::int64_t seenPts = 0;
    };

    struct Release {
        Point at;
        std::int64_t atUs = 0;
    };

    struct Shown {
        std::int64_t ptsUs = 0;
        std::int64_t shownUs = 0;
    };

    [[nodiscard]] double pixelsApart(Point a, Point b) const noexcept;
    [[nodiscard]] double dp(double value) const noexcept { return value * m_pixelsPerDp; }
    [[nodiscard]] bool crowded(Point at, std::int64_t nowUs) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> shownAfter(std::int64_t ptsUs) const noexcept;
    void record(std::int64_t latencyUs);
    void conclude();

    const LatencyMeterOptions m_options;
    mutable std::mutex m_mutex;
    bool m_running = true;
    std::uint32_t m_nextId = 1;
    std::vector<Probe> m_probes;
    std::map<std::uint8_t, Point> m_pressed;
    std::vector<Release> m_releases;
    std::array<Shown, 32> m_shown{};
    std::size_t m_shownNext = 0;
    std::size_t m_shownCount = 0;
    std::vector<double> m_samplesMs;
    int m_missed = 0;
    std::optional<LatencyResult> m_result;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    double m_pixelsPerDp = 0;
};

}
