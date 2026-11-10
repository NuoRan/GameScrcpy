#include "LatencyMeter.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gs::app {

namespace {

constexpr double kFixedScale = 65535.0;

}

LatencyMeter::LatencyMeter() : LatencyMeter(LatencyMeterOptions{}) {}

LatencyMeter::LatencyMeter(LatencyMeterOptions options) : m_options(options) {}

double LatencyMeter::pixelsApart(Point a, Point b) const noexcept
{
    return std::hypot((a.u - b.u) * m_frameWidth, (a.v - b.v) * m_frameHeight);
}

bool LatencyMeter::crowded(Point at, std::int64_t nowUs) const noexcept
{
    if (m_frameWidth <= 0 || m_frameHeight <= 0) {
        return false;
    }
    const double reach = dp(m_options.ringToDp);
    const bool fading = std::ranges::any_of(m_releases, [&](const Release& release) {
        return nowUs - release.atUs < m_options.settleUs && pixelsApart(release.at, at) < reach;
    });
    const bool beside = std::ranges::any_of(m_pressed, [&](const auto& pressed) {
        return pixelsApart(pressed.second, at) < reach;
    });
    return fading || beside;
}

void LatencyMeter::touched(const protocol::TouchFrame& frame, std::int64_t nowUs)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_running) {
        return;
    }
    std::erase_if(m_releases, [&](const Release& release) { return nowUs - release.atUs >= m_options.settleUs; });
    for (const protocol::TouchPoint& point : frame.points) {
        const Point at{point.x / kFixedScale, point.y / kFixedScale};
        const bool active = m_pressed.contains(point.pointerId);
        switch (point.action) {
        case protocol::TouchAction::Down:
            if (!active) {
                const bool watchable = std::ssize(m_probes) < m_options.mostWatched && !crowded(at, nowUs);
                m_pressed[point.pointerId] = at;
                if (watchable) {
                    Probe probe;
                    probe.id = m_nextId++;
                    probe.pointerId = point.pointerId;
                    probe.at = at;
                    probe.pressUs = nowUs;
                    m_probes.push_back(probe);
                }
                break;
            }
            [[fallthrough]];
        case protocol::TouchAction::Move:
            if (active) {
                m_pressed[point.pointerId] = at;
                std::erase_if(m_probes, [&](const Probe& probe) {
                    return probe.held && probe.pointerId == point.pointerId && probe.stage != Stage::Seen &&
                           m_frameWidth > 0 && pixelsApart(probe.at, at) > dp(m_options.moveDp);
                });
            }
            break;
        case protocol::TouchAction::Up:
        case protocol::TouchAction::Cancel:
            if (active) {
                m_releases.push_back({m_pressed[point.pointerId], nowUs});
                m_pressed.erase(point.pointerId);
            }
            for (Probe& probe : m_probes) {
                if (probe.pointerId == point.pointerId) {
                    probe.held = false;
                }
            }
            break;
        }
    }
}

LatencyMeter::Plan LatencyMeter::plan(int frameWidth, int frameHeight, double pixelsPerDp, std::int64_t nowUs)
{
    Plan result;
    const std::scoped_lock lock(m_mutex);
    if (!m_running || frameWidth <= 0 || frameHeight <= 0 || !(pixelsPerDp > 0)) {
        return result;
    }
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    m_pixelsPerDp = pixelsPerDp;
    std::erase_if(m_probes, [&](const Probe& probe) {
        const std::int64_t age = nowUs - probe.pressUs;
        if (probe.stage == Stage::Seen) {
            return age > 2 * m_options.detectWithinUs;
        }
        if (age > m_options.detectWithinUs) {
            ++m_missed;
            return true;
        }
        return false;
    });
    if (m_missed >= m_options.mostMissed && m_samplesMs.empty()) {
        conclude();
        return result;
    }
    const int core = std::max(1, static_cast<int>(std::lround(dp(m_options.coreDp))));
    const int ringFrom = std::max(core + 2, static_cast<int>(std::lround(dp(m_options.ringFromDp))));
    const int ringTo = std::max(ringFrom + 2, static_cast<int>(std::lround(dp(m_options.ringToDp))));
    for (const Probe& probe : m_probes) {
        const media::LumaSpot spot{static_cast<int>(std::lround(probe.at.u * (frameWidth - 1))),
                                   static_cast<int>(std::lround(probe.at.v * (frameHeight - 1))), core, ringFrom,
                                   ringTo};
        if (probe.stage == Stage::Fresh) {
            result.fresh.push_back(probe.id);
            result.freshSpots.push_back(spot);
        } else if (probe.stage != Stage::Seen) {
            result.watched.push_back(probe.id);
            result.watchedSpots.push_back(spot);
        }
    }
    return result;
}

void LatencyMeter::observe(const Plan& plan, std::span<const media::LumaReading> fresh,
                           std::span<const media::LumaReading> watched, std::int64_t ptsUs, std::int64_t)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_running) {
        return;
    }
    const auto find = [&](std::uint32_t id) { return std::ranges::find(m_probes, id, &Probe::id); };
    std::vector<std::uint32_t> done;
    for (std::size_t i = 0; i < plan.fresh.size(); ++i) {
        const auto probe = find(plan.fresh[i]);
        if (probe == m_probes.end() || probe->stage != Stage::Fresh) {
            continue;
        }
        if (i < fresh.size()) {
            probe->base = fresh[i];
            probe->stage = Stage::Watching;
        } else {
            done.push_back(probe->id);
        }
    }
    for (std::size_t i = 0; i < plan.watched.size() && i < watched.size(); ++i) {
        const auto probe = find(plan.watched[i]);
        if (probe == m_probes.end()) {
            continue;
        }
        const double change = std::abs(watched[i].core - probe->base.core);
        const double ringChange = std::abs(watched[i].ring - probe->base.ring);
        const bool dot =
            change >= m_options.minimumChange && change >= m_options.ringFactor * ringChange + m_options.ringSlack;
        if (probe->stage == Stage::Watching) {
            if (dot) {
                probe->stage = Stage::Candidate;
                probe->candidatePts = ptsUs;
            }
        } else if (probe->stage == Stage::Candidate) {
            if (dot || ptsUs - probe->candidatePts > m_options.stillGapUs) {
                probe->stage = Stage::Seen;
                probe->seenPts = probe->candidatePts;
            } else {
                probe->stage = Stage::Watching;
            }
        }
        if (probe->stage == Stage::Seen) {
            if (const auto shownUs = shownAfter(probe->seenPts)) {
                const std::int64_t latencyUs = *shownUs - probe->pressUs;
                done.push_back(probe->id);
                record(latencyUs);
                if (!m_running) {
                    return;
                }
            }
        }
    }
    std::erase_if(m_probes, [&](const Probe& probe) { return std::ranges::find(done, probe.id) != done.end(); });
}

void LatencyMeter::shown(std::int64_t ptsUs, std::int64_t shownUs)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_running) {
        return;
    }
    m_shown[m_shownNext] = {ptsUs, shownUs};
    m_shownNext = (m_shownNext + 1) % m_shown.size();
    m_shownCount = std::min(m_shownCount + 1, m_shown.size());
    std::vector<std::int64_t> latencies;
    std::erase_if(m_probes, [&](const Probe& probe) {
        if (probe.stage != Stage::Seen || ptsUs < probe.seenPts) {
            return false;
        }
        latencies.push_back(shownUs - probe.pressUs);
        return true;
    });
    for (const std::int64_t latencyUs : latencies) {
        if (m_running) {
            record(latencyUs);
        }
    }
}

void LatencyMeter::finish()
{
    const std::scoped_lock lock(m_mutex);
    if (m_running) {
        conclude();
    }
}

LatencyProgress LatencyMeter::progress() const
{
    const std::scoped_lock lock(m_mutex);
    LatencyProgress progress;
    progress.running = m_running;
    progress.measured = static_cast<int>(m_samplesMs.size());
    progress.target = m_options.target;
    progress.missed = m_missed;
    progress.result = m_result;
    return progress;
}

std::optional<std::int64_t> LatencyMeter::shownAfter(std::int64_t ptsUs) const noexcept
{
    for (std::size_t age = m_shownCount; age > 0; --age) {
        const Shown& entry = m_shown[(m_shownNext + m_shown.size() - age) % m_shown.size()];
        if (entry.ptsUs >= ptsUs) {
            return entry.shownUs;
        }
    }
    return std::nullopt;
}

void LatencyMeter::record(std::int64_t latencyUs)
{
    if (latencyUs <= 0 || latencyUs > 2 * m_options.detectWithinUs) {
        return;
    }
    m_samplesMs.push_back(static_cast<double>(latencyUs) / 1000.0);
    if (std::ssize(m_samplesMs) >= m_options.target) {
        conclude();
    }
}

void LatencyMeter::conclude()
{
    m_running = false;
    m_probes.clear();
    if (std::ssize(m_samplesMs) < m_options.fewest) {
        return;
    }
    std::vector<double> sorted = m_samplesMs;
    std::ranges::sort(sorted);
    const std::size_t count = sorted.size();
    LatencyResult result;
    result.samples = static_cast<int>(count);
    result.medianMs = count % 2 == 1 ? sorted[count / 2] : (sorted[count / 2 - 1] + sorted[count / 2]) / 2;
    result.lowMs = sorted.front();
    result.highMs = sorted.back();
    m_result = result;
}

}
