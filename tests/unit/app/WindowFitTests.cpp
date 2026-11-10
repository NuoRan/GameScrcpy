#include "WindowFit.h"

#include <doctest/doctest.h>

using gs::app::fitWindow;
using gs::app::settings::WindowPlacement;

namespace {

const QRect kPrimary(0, 0, 1920, 1032);
const QRect kRight(1920, 0, 2560, 1400);
const QSize kFallback(1180, 760);
const QSize kMinimum(880, 560);
const QRect kCentered(370, 136, 1180, 760);

}

TEST_CASE("a window never placed is centered on the primary screen")
{
    CHECK(fitWindow({}, {kPrimary, kRight}, kFallback, kMinimum) == kCentered);
}

TEST_CASE("a window goes back where it was, on any screen")
{
    CHECK(fitWindow({100, 80, 1300, 820, false}, {kPrimary}, kFallback, kMinimum) == QRect(100, 80, 1300, 820));
    CHECK(fitWindow({2100, 200, 1400, 900, true}, {kPrimary, kRight}, kFallback, kMinimum) ==
          QRect(2100, 200, 1400, 900));
}

TEST_CASE("a window partly off the screens stays while its top strip can be grabbed")
{
    CHECK(fitWindow({1720, 300, 1180, 760, false}, {kPrimary}, kFallback, kMinimum) == QRect(1720, 300, 1180, 760));
}

TEST_CASE("a window out of reach is centered on the primary screen")
{
    CHECK(fitWindow({2100, 200, 1400, 900, false}, {kPrimary}, kFallback, kMinimum) == QRect(260, 66, 1400, 900));
    CHECK(fitWindow({100, -500, 1180, 760, false}, {kPrimary}, kFallback, kMinimum) == kCentered);
    CHECK(fitWindow({1850, 100, 1180, 760, false}, {kPrimary}, kFallback, kMinimum) == kCentered);
}

TEST_CASE("the size fits the screen and keeps the minimum")
{
    CHECK(fitWindow({0, 0, 2400, 1400, false}, {kPrimary}, kFallback, kMinimum) == QRect(0, 0, 1920, 1032));
    CHECK(fitWindow({50, 50, 400, 300, false}, {kPrimary}, kFallback, kMinimum) == QRect(50, 50, 880, 560));
}
