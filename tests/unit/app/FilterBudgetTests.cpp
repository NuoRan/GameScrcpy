#include "FilterBudget.h"

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

using gs::app::FilterBudget;
using gs::render::FilterStatus;
using namespace std::chrono_literals;

namespace {

FilterStatus filter(std::string id, std::vector<double> passMs, bool ready = true)
{
    return {.id = std::move(id), .ready = ready, .passMs = std::move(passMs)};
}

const FilterBudget::Clock::time_point kStart{};

}

TEST_CASE("filters within the frame budget never raise a notice")
{
    FilterBudget budget;
    for (int second = 0; second < 30; ++second) {
        CHECK_FALSE(budget.observe({filter("a/Glow", {0.8, 0.7}), filter("b/Scan", {0.4})},
                                   kStart + std::chrono::seconds(second))
                        .has_value());
    }
}

TEST_CASE("a frame cost of exactly the budget is not over it")
{
    FilterBudget budget;
    for (int second = 0; second < 30; ++second) {
        CHECK_FALSE(budget.observe({filter("a/Glow", {2.0})}, kStart + std::chrono::seconds(second)).has_value());
    }
}

TEST_CASE("a frame cost over the budget for five seconds raises one notice that names the heaviest filter")
{
    FilterBudget budget;
    const std::vector<FilterStatus> heavy{filter("a/Glow", {1.5, 0.5}), filter("b/Scan", {0.3}),
                                          filter("c/Broken", {9.0}, false)};
    CHECK_FALSE(budget.observe(heavy, kStart).has_value());
    CHECK_FALSE(budget.observe(heavy, kStart + 4900ms).has_value());
    const auto notice = budget.observe(heavy, kStart + 5s);
    REQUIRE(notice.has_value());
    CHECK(notice->heaviest == "a/Glow");
    CHECK(notice->heaviestMs == doctest::Approx(2.0));
    CHECK(notice->totalMs == doctest::Approx(2.3));
    CHECK_FALSE(budget.observe(heavy, kStart + 60s).has_value());
}

TEST_CASE("dropping back under the budget restarts the count, and a notice comes again only after five seconds under it")
{
    FilterBudget budget;
    const std::vector<FilterStatus> heavy{filter("a/Glow", {2.5})};
    const std::vector<FilterStatus> light{filter("a/Glow", {1.0})};
    CHECK_FALSE(budget.observe(heavy, kStart).has_value());
    CHECK_FALSE(budget.observe(light, kStart + 3s).has_value());
    CHECK_FALSE(budget.observe(heavy, kStart + 4s).has_value());
    CHECK_FALSE(budget.observe(heavy, kStart + 8s).has_value());
    CHECK(budget.observe(heavy, kStart + 9s).has_value());
    CHECK_FALSE(budget.observe(light, kStart + 10s).has_value());
    CHECK_FALSE(budget.observe(heavy, kStart + 11s).has_value());
    CHECK_FALSE(budget.observe(heavy, kStart + 16s).has_value());
    CHECK_FALSE(budget.observe(light, kStart + 17s).has_value());
    CHECK_FALSE(budget.observe(light, kStart + 22s).has_value());
    CHECK_FALSE(budget.observe(heavy, kStart + 23s).has_value());
    CHECK(budget.observe(heavy, kStart + 28s).has_value());
}

TEST_CASE("a new set of filters starts the count again even right after a notice")
{
    FilterBudget budget;
    const std::vector<FilterStatus> heavy{filter("a/Glow", {2.5})};
    CHECK_FALSE(budget.observe(heavy, kStart).has_value());
    CHECK(budget.observe(heavy, kStart + 5s).has_value());
    budget.reset();
    CHECK_FALSE(budget.observe(heavy, kStart + 6s).has_value());
    CHECK(budget.observe(heavy, kStart + 11s).has_value());
}
