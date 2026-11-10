#include "ExtensionBisect.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using gs::app::ExtensionBisect;
using Outcome = ExtensionBisect::Outcome;

namespace {

std::vector<std::string> idsUpTo(int count)
{
    std::vector<std::string> ids;
    for (int index = 0; index < count; ++index) {
        ids.push_back("e" + std::to_string(index));
    }
    return ids;
}

int findCulprit(ExtensionBisect& bisect, const std::string& culprit)
{
    int answers = 0;
    while (bisect.outcome() == Outcome::Running) {
        bisect.answer(bisect.loads(culprit));
        ++answers;
    }
    return answers;
}

}

TEST_CASE("each answer halves the suspects until the one extension behind the problem is left")
{
    for (int count = 1; count <= 9; ++count) {
        for (int index = 0; index < count; ++index) {
            const std::string culprit = "e" + std::to_string(index);
            ExtensionBisect bisect = ExtensionBisect::begin(idsUpTo(count));
            CHECK(bisect.trial().empty());
            const int answers = findCulprit(bisect, culprit);
            INFO(count << " extensions, the problem in " << culprit);
            CHECK(bisect.outcome() == Outcome::Found);
            CHECK(bisect.found() == culprit);
            CHECK(bisect.trial().empty());
            CHECK(answers <= bisect.steps());
        }
    }
}

TEST_CASE("the first step loads nothing, and a problem that stays then is not from extensions")
{
    ExtensionBisect bisect = ExtensionBisect::begin({"bob.two", "alice.one", "bob.two"});
    CHECK(bisect.original() == std::vector<std::string>{"alice.one", "bob.two"});
    CHECK(bisect.step() == 0);
    CHECK(bisect.steps() == 2);
    CHECK_FALSE(bisect.loads("alice.one"));
    bisect.answer(true);
    CHECK(bisect.outcome() == Outcome::NotExtensions);
    CHECK_FALSE(bisect.found().has_value());
    CHECK(bisect.trial().empty());
    bisect.answer(false);
    CHECK(bisect.outcome() == Outcome::NotExtensions);
}

TEST_CASE("with no extension switched on there is nothing to look for")
{
    const ExtensionBisect bisect = ExtensionBisect::begin({});
    CHECK(bisect.outcome() == Outcome::NotExtensions);
    CHECK(bisect.steps() == 1);
}

TEST_CASE("the state survives a save and a load, and a damaged or contradictory file is refused")
{
    ExtensionBisect bisect = ExtensionBisect::begin(idsUpTo(5));
    bisect.answer(false);
    bisect.answer(true);
    const std::optional<ExtensionBisect> loaded = ExtensionBisect::load(bisect.save());
    REQUIRE(loaded.has_value());
    CHECK(loaded->original() == bisect.original());
    CHECK(loaded->suspects() == bisect.suspects());
    CHECK(loaded->trial() == bisect.trial());
    CHECK(loaded->step() == bisect.step());
    CHECK(loaded->outcome() == bisect.outcome());

    CHECK_FALSE(ExtensionBisect::load("").has_value());
    CHECK_FALSE(ExtensionBisect::load("{}").has_value());
    CHECK_FALSE(ExtensionBisect::load(
                    R"({"original": ["a"], "suspects": ["a"], "trial": ["b"], "step": 1, "outcome": "running"})")
                    .has_value());
    CHECK_FALSE(ExtensionBisect::load(
                    R"({"original": ["a"], "suspects": ["a", "b"], "trial": [], "step": 1, "outcome": "running"})")
                    .has_value());
    CHECK_FALSE(ExtensionBisect::load(
                    R"({"original": ["a", "b"], "suspects": ["a", "b"], "trial": [], "step": 2, "outcome": "found"})")
                    .has_value());
    CHECK_FALSE(ExtensionBisect::load(
                    R"({"original": ["a", "a"], "suspects": ["a"], "trial": [], "step": 1, "outcome": "running"})")
                    .has_value());
    CHECK_FALSE(ExtensionBisect::load(
                    R"({"original": ["a"], "suspects": ["a"], "trial": [], "step": -1, "outcome": "running"})")
                    .has_value());
    CHECK_FALSE(ExtensionBisect::load(
                    R"({"original": ["a"], "suspects": ["a"], "trial": [], "step": 0, "outcome": "lost"})")
                    .has_value());
}
