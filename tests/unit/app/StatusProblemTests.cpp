#include "StatusProblem.h"

#include <doctest/doctest.h>

using gs::app::measuredProblem;
using gs::app::ProblemFilter;
using gs::app::StatusProblem;

TEST_CASE("an input to render delay above 100 ms is a problem, whatever the frame rate")
{
    CHECK(measuredProblem(112.0, 60, 60, 60) == StatusProblem::SlowDelay);
    CHECK(measuredProblem(100.0, 60, 60, 60) == StatusProblem::None);
    CHECK(measuredProblem(std::nullopt, 60, 60, 60) == StatusProblem::None);
}

TEST_CASE("a still picture sends few frames and is not a problem")
{
    CHECK(measuredProblem(20.0, 3, 3, 60) == StatusProblem::None);
    CHECK(measuredProblem(20.0, 19, 0, 60) == StatusProblem::None);
}

TEST_CASE("frames that arrive but are not shown are a problem")
{
    CHECK(measuredProblem(20.0, 60, 30, 60) == StatusProblem::DroppingFrames);
    CHECK(measuredProblem(20.0, 60, 43, 60) == StatusProblem::None);
}

TEST_CASE("a phone faster than the monitor is measured against the monitor")
{
    CHECK(measuredProblem(20.0, 120, 60, 60) == StatusProblem::None);
    CHECK(measuredProblem(20.0, 120, 40, 60) == StatusProblem::DroppingFrames);
    CHECK(measuredProblem(20.0, 120, 100, 0) == StatusProblem::None);
}

TEST_CASE("a problem shows after two samples in a row and clears at once")
{
    ProblemFilter filter;
    CHECK(filter.update(StatusProblem::SlowDelay) == StatusProblem::None);
    CHECK(filter.update(StatusProblem::SlowDelay) == StatusProblem::SlowDelay);
    CHECK(filter.update(StatusProblem::SlowDelay) == StatusProblem::SlowDelay);
    CHECK(filter.update(StatusProblem::None) == StatusProblem::None);
    CHECK(filter.shown() == StatusProblem::None);
}

TEST_CASE("a different problem needs its own two samples")
{
    ProblemFilter filter;
    (void)filter.update(StatusProblem::SlowDelay);
    REQUIRE(filter.update(StatusProblem::SlowDelay) == StatusProblem::SlowDelay);
    CHECK(filter.update(StatusProblem::DroppingFrames) == StatusProblem::SlowDelay);
    CHECK(filter.update(StatusProblem::DroppingFrames) == StatusProblem::DroppingFrames);
}

TEST_CASE("a single slow sample between good ones never shows")
{
    ProblemFilter filter;
    CHECK(filter.update(StatusProblem::SlowDelay) == StatusProblem::None);
    CHECK(filter.update(StatusProblem::None) == StatusProblem::None);
    CHECK(filter.update(StatusProblem::SlowDelay) == StatusProblem::None);
}
