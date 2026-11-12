#include "gs/ext/Version.h"

#include <doctest/doctest.h>

#include <compare>
#include <iterator>

using gs::ext::Version;
using gs::ext::VersionRange;

namespace {

Version version(const char* text)
{
    const std::optional<Version> parsed = Version::parse(text);
    REQUIRE_MESSAGE(parsed.has_value(), text);
    return *parsed;
}

bool inRange(const char* range, const char* text)
{
    const std::optional<VersionRange> parsed = VersionRange::parse(range);
    REQUIRE_MESSAGE(parsed.has_value(), range);
    return parsed->contains(version(text));
}

}

TEST_CASE("versions read as MAJOR.MINOR.PATCH with an optional pre-release tag, and nothing else")
{
    CHECK(version("1.2.3") == Version{1, 2, 3, {}});
    CHECK(version("0.0.0").text() == "0.0.0");
    CHECK(version("2.0.0-beta.1").prerelease == "beta.1");
    CHECK(version("10.20.30-rc-1").text() == "10.20.30-rc-1");
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.3-", "1.2.3-beta..1", "1.2.3-01",
                            "1.2.3+build", "v1.2.3", " 1.2.3", "1.2.x", "1234567890.0.0", "1.2.3-be ta"}) {
        CHECK_MESSAGE(!Version::parse(bad).has_value(), bad);
    }
}

TEST_CASE("versions order by semantic version precedence")
{
    const char* ordered[] = {"0.9.9",        "1.0.0-alpha",   "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                             "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",    "1.0.0",            "1.0.1",
                             "1.1.0",        "2.0.0",         "10.0.0"};
    for (std::size_t i = 0; i + 1 < std::size(ordered); ++i) {
        CHECK_MESSAGE(version(ordered[i]) < version(ordered[i + 1]), ordered[i]);
    }
    CHECK(std::is_eq(version("1.2.3") <=> version("1.2.3")));
    CHECK(version("1.2.3-a") != version("1.2.3"));
}

TEST_CASE("ranges: exact, caret, tilde, comparator sets and any")
{
    CHECK(inRange("1.2.3", "1.2.3"));
    CHECK_FALSE(inRange("1.2.3", "1.2.4"));
    CHECK(inRange("=1.2.3", "1.2.3"));
    CHECK(inRange("^1.2", "1.2.0"));
    CHECK(inRange("^1.2", "1.9.9"));
    CHECK_FALSE(inRange("^1.2", "2.0.0"));
    CHECK_FALSE(inRange("^1.2", "1.1.9"));
    CHECK(inRange("^1.2.3", "1.2.3"));
    CHECK_FALSE(inRange("^1.2.3", "1.2.2"));
    CHECK(inRange("^0.2.3", "0.2.9"));
    CHECK_FALSE(inRange("^0.2.3", "0.3.0"));
    CHECK(inRange("^0.0.3", "0.0.3"));
    CHECK_FALSE(inRange("^0.0.3", "0.0.4"));
    CHECK(inRange("^0.0", "0.0.9"));
    CHECK_FALSE(inRange("^0.0", "0.1.0"));
    CHECK(inRange("^1", "1.9.0"));
    CHECK_FALSE(inRange("^1", "2.0.0"));
    CHECK(inRange("~1.2.3", "1.2.9"));
    CHECK_FALSE(inRange("~1.2.3", "1.3.0"));
    CHECK(inRange("~1.2", "1.2.0"));
    CHECK(inRange("~1", "1.5.0"));
    CHECK_FALSE(inRange("~1", "2.0.0"));
    CHECK(inRange(">=1.0 <3", "1.0.0"));
    CHECK(inRange(">=1.0 <3", "2.9.9"));
    CHECK_FALSE(inRange(">=1.0 <3", "3.0.0"));
    CHECK_FALSE(inRange(">=1.0 <3", "0.9.9"));
    CHECK(inRange(">1 <=2.5", "2.5.0"));
    CHECK_FALSE(inRange(">1 <=2.5", "1.0.0"));
    CHECK(inRange("*", "0.0.1"));
    CHECK(inRange("  ^1.2  ", "1.3.0"));
    CHECK(VersionRange::parse("^1.2")->text() == "^1.2");
    CHECK(VersionRange::any().contains(version("5.0.0")));
}

TEST_CASE("a pre-release is in a range only when a bound names the same release with a tag")
{
    CHECK_FALSE(inRange("^1.2", "2.0.0-beta"));
    CHECK_FALSE(inRange("^1.2", "1.3.0-beta"));
    CHECK(inRange("^1.2.3-beta", "1.2.3-beta.2"));
    CHECK_FALSE(inRange("^1.2.3-beta", "1.2.4-beta"));
    CHECK(inRange("^1.2.3-beta", "1.2.4"));
    CHECK_FALSE(inRange("*", "1.0.0-rc.1"));
    CHECK(inRange("1.0.0-rc.1", "1.0.0-rc.1"));
}

TEST_CASE("ranges that are not one of the supported forms do not parse")
{
    for (const char* bad : {"", "^", "~", "^1.x", ">= 1.0", "1.2", "=1.2", "1.2.3 ||| 2", ">=1.0 <", "^^1", "latest",
                            "1.2.3 - 2.0.0"}) {
        CHECK_MESSAGE(!VersionRange::parse(bad).has_value(), bad);
    }
}

TEST_CASE("ADR-0113: a version is above a range when every version the range allows lies below it")
{
    const auto above = [](const char* range, const char* text) {
        const std::optional<VersionRange> parsed = VersionRange::parse(range);
        REQUIRE_MESSAGE(parsed.has_value(), range);
        return parsed->aboveAll(version(text));
    };
    CHECK(above("^1.0", "2.0.0"));
    CHECK(above("~1.2", "1.3.0"));
    CHECK(above("1.2.3", "1.2.4"));
    CHECK(above("=1.2.3", "1.2.4"));
    CHECK(above(">=1.0 <3", "3.0.0"));
    CHECK(above(">1 <=2.5", "2.6.0"));
    CHECK(above(">=1.0 <2.0 <1.5", "1.6.0"));
    CHECK_FALSE(above("^1.0", "1.9.9"));
    CHECK_FALSE(above("^1.0", "1.0.0"));
    CHECK_FALSE(above("^1.0", "0.9.0"));
    CHECK_FALSE(above("~1.2", "1.2.9"));
    CHECK_FALSE(above("1.2.3", "1.2.2"));
    CHECK_FALSE(above("<2.0", "1.0.0"));
    CHECK_FALSE(above(">=1.0 <3", "2.9.9"));
    CHECK_FALSE(above(">1 <=2.5", "2.5.0"));
    CHECK_FALSE(above(">=1.0 <2.0 <1.5", "1.4.0"));
    CHECK_FALSE(above(">=1.0", "9.0.0"));
    CHECK_FALSE(above("*", "9.0.0"));
    CHECK_FALSE(above("^1.2.3-beta", "2.0.0-beta"));
}
