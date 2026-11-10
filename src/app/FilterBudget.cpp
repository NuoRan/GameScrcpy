#include "FilterBudget.h"

#include <numeric>

namespace gs::app {

std::optional<BudgetNotice> FilterBudget::observe(const std::vector<render::FilterStatus>& status,
                                                  Clock::time_point now)
{
    BudgetNotice notice;
    for (const render::FilterStatus& filter : status) {
        if (!filter.ready) {
            continue;
        }
        const double spent = std::accumulate(filter.passMs.begin(), filter.passMs.end(), 0.0);
        notice.totalMs += spent;
        if (spent > notice.heaviestMs) {
            notice.heaviestMs = spent;
            notice.heaviest = filter.id;
        }
    }
    if (notice.totalMs <= kFrameBudgetMs) {
        m_overSince.reset();
        if (!m_underSince) {
            m_underSince = now;
        }
        if (now - *m_underSince >= kSustained) {
            m_noticed = false;
        }
        return std::nullopt;
    }
    m_underSince.reset();
    if (!m_overSince) {
        m_overSince = now;
    }
    if (m_noticed || now - *m_overSince < kSustained) {
        return std::nullopt;
    }
    m_noticed = true;
    return notice;
}

void FilterBudget::reset() noexcept
{
    m_overSince.reset();
    m_underSince.reset();
    m_noticed = false;
}

}
