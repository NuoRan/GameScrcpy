#include "LoadMeter.h"

#include <algorithm>

namespace gs::script::detail {

LoadMeter::LoadMeter(const Limits& limits) noexcept
    : m_spike(limits.spikeThreshold)
    , m_window(limits.sustainedWindow)
    , m_threshold(limits.sustainedBusy)
{}

LoadMeter::Verdict LoadMeter::record(Microseconds end, Microseconds duration) noexcept
{
    Verdict verdict;
    verdict.spike = m_spike.count() > 0 && duration >= m_spike;
    if (m_window.count() <= 0 || m_threshold.count() <= 0) {
        return verdict;
    }
    const std::int64_t width =
        std::max<std::int64_t>(1, m_window.count() / static_cast<std::int64_t>(kBuckets));
    const std::int64_t index = std::max<std::int64_t>(0, end.count()) / width;
    Bucket& bucket = m_buckets[static_cast<std::size_t>(index) % kBuckets];
    if (bucket.index != index) {
        bucket.index = index;
        bucket.busy = Microseconds{0};
    }
    bucket.busy += duration;
    const std::int64_t oldest = index - static_cast<std::int64_t>(kBuckets);
    for (const Bucket& entry : m_buckets) {
        if (entry.index > oldest && entry.index <= index) {
            verdict.busy += entry.busy;
        }
    }
    if (!m_sustained && verdict.busy >= m_threshold) {
        m_sustained = true;
        verdict.sustained = true;
    } else if (m_sustained && verdict.busy < m_threshold / 2) {
        m_sustained = false;
    }
    return verdict;
}

}
