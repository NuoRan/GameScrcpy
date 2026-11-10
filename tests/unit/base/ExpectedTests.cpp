#include "gs/base/Expected.h"

#include <doctest/doctest.h>

#include <string>

namespace {

gs::Expected<int, std::string> parseDigit(char c)
{
    if (c < '0' || c > '9') {
        return gs::makeUnexpected(std::string("not a digit"));
    }
    return c - '0';
}

}

TEST_CASE("Expected carries either a value or an error")
{
    const auto ok = parseDigit('7');
    REQUIRE(ok.has_value());
    CHECK(*ok == 7);

    const auto failed = parseDigit('x');
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error() == "not a digit");
}

TEST_CASE("Expected composes with and_then without branching at the call site")
{
    const auto doubled = parseDigit('4').and_then([](int v) -> gs::Expected<int, std::string> {
        return v * 2;
    });
    REQUIRE(doubled.has_value());
    CHECK(*doubled == 8);
}
