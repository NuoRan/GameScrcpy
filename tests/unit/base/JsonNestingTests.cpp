#include "gs/base/JsonNesting.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>

namespace {

static_assert(gs::jsonNestingWithin("", 0));
static_assert(gs::jsonNestingWithin("1", 0));
static_assert(!gs::jsonNestingWithin("[]", 0));
static_assert(gs::jsonNestingWithin("[{}]", 2));
static_assert(!gs::jsonNestingWithin("[{}]", 1));

std::string nested(std::size_t levels)
{
    return std::string(levels, '[') + std::string(levels, ']');
}

}

TEST_CASE("ADR-0112: JSON nested as deep as allowed passes the check and one level more does not")
{
    CHECK(gs::jsonNestingWithin(nested(gs::kMostJsonNesting), gs::kMostJsonNesting));
    CHECK_FALSE(gs::jsonNestingWithin(nested(gs::kMostJsonNesting + 1), gs::kMostJsonNesting));
    CHECK_FALSE(gs::jsonNestingWithin(nested(200'000), gs::kMostJsonNesting));
    CHECK(gs::jsonNestingWithin(R"({"a":[1,{"b":[]}],"c":{}})", 4));
    CHECK_FALSE(gs::jsonNestingWithin(R"({"a":[1,{"b":[]}],"c":{}})", 3));
    CHECK(gs::jsonNestingWithin(R"([[1],[2],[3]])", 2));
}

TEST_CASE("ADR-0112: brackets inside strings do not count towards the nesting of JSON, and escapes are followed")
{
    CHECK(gs::jsonNestingWithin(R"(["[[[[", "{{{{\"[[[[", "\\"])", 1));
    CHECK_FALSE(gs::jsonNestingWithin(R"(["\\", [[]]])", 2));
    CHECK(gs::jsonNestingWithin(R"({"key[[[":"\u0022[[["})", 1));
}
