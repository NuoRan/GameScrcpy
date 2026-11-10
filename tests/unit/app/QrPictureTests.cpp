#include "QrPicture.h"

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>

using gs::app::darkModulesPath;
using gs::app::qrPicture;

TEST_CASE("dark modules become one rectangle per run in each row")
{
    constexpr std::array<std::array<bool, 3>, 3> grid{{{true, true, false}, {false, true, true}, {true, false, true}}};
    const std::string path = darkModulesPath(3, [&](int x, int y) { return grid[y][x]; });
    CHECK(path == "M0 0h2v1h-2zM1 1h2v1h-2zM0 2h1v1h-1zM2 2h1v1h-1z");
    CHECK(darkModulesPath(2, [](int, int) { return false; }).empty());
}

TEST_CASE("a pairing QR code: a valid version with its finder patterns")
{
    const auto picture = qrPicture("WIFI:T:ADB;S:gamescrcpy-Xy7Qp2Lk;P:a8F3kQz2Lm9W;;");
    REQUIRE(picture.size >= 21);
    CHECK((picture.size - 17) % 4 == 0);
    CHECK(picture.path.starts_with("M0 0h7v1h-7z"));
    CHECK(picture.path.find(std::format("M{} 0h7v1h-7z", picture.size - 7)) != std::string::npos);
    CHECK(qrPicture("WIFI:T:ADB;S:gamescrcpy-Xy7Qp2Lk;P:a8F3kQz2Lm9W;;") == picture);
}

TEST_CASE("text too long for any QR code gives no picture")
{
    const auto picture = qrPicture(std::string(8000, 'x'));
    CHECK(picture.size == 0);
    CHECK(picture.path.empty());
}
