#include "PendingArrivals.h"

#include <algorithm>

namespace gs::media {

void PendingArrivals::sent(std::int64_t ptsUs, std::int64_t receivedUs) noexcept
{
    if (m_count == m_entries.size()) {
        std::shift_left(m_entries.begin(), m_entries.end(), 1);
        --m_count;
        ++m_dropped;
    }
    m_entries[m_count++] = Entry{.ptsUs = ptsUs, .receivedUs = receivedUs};
}

std::optional<std::int64_t> PendingArrivals::returned(std::int64_t ptsUs) noexcept
{
    std::size_t match = 0;
    while (match < m_count && m_entries[match].ptsUs != ptsUs) {
        ++match;
    }
    if (match == m_count) {
        return std::nullopt;
    }
    const std::int64_t receivedUs = m_entries[match].receivedUs;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < m_count; ++i) {
        if (i != match && m_entries[i].ptsUs >= ptsUs) {
            m_entries[kept++] = m_entries[i];
        }
    }
    m_count = kept;
    return receivedUs;
}

void PendingArrivals::forget(std::int64_t ptsUs) noexcept
{
    for (std::size_t i = m_count; i > 0; --i) {
        if (m_entries[i - 1].ptsUs != ptsUs) {
            continue;
        }
        for (std::size_t next = i; next < m_count; ++next) {
            m_entries[next - 1] = m_entries[next];
        }
        --m_count;
        return;
    }
}

void PendingArrivals::clear() noexcept
{
    m_count = 0;
    m_dropped = 0;
}

}
