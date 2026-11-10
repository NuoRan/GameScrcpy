#pragma once

#include <gs/render/VideoFilter.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace gs::app {

struct BudgetNotice {
    double totalMs = 0.0;
    std::string heaviest;
    double heaviestMs = 0.0;
};

class FilterBudget
{
public:
    using Clock = std::chrono::steady_clock;

    static constexpr double kFrameBudgetMs = 2.0;
    static constexpr std::chrono::seconds kSustained{5};

    [[nodiscard]] std::optional<BudgetNotice> observe(const std::vector<render::FilterStatus>& status,
                                                      Clock::time_point now);
    void reset() noexcept;

private:
    std::optional<Clock::time_point> m_overSince;
    std::optional<Clock::time_point> m_underSince;
    bool m_noticed = false;
};

}
