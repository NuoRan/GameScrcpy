#include "Fixture.h"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::blueprint;
using namespace blueprint_test;

namespace {

Environment withClock()
{
    nlohmann::json report = nlohmann::json::parse(readFile(std::filesystem::path(GS_SDK_DIR) / "api-report.json"));
    report["namespaces"].push_back(
        {{"name", "ext.gstest.clock"},
         {"functions",
          nlohmann::json::array({{{"name", "now"}, {"signature", "now(): string"}},
                                 {{"name", "ring"}, {"signature", "ring(times: number): void"}}})}});
    Environment made = environment();
    made.api = loadApi(report.dump(), readFile(std::filesystem::path(GS_SDK_DIR) / "types" / "gamescrcpy.d.ts"));
    made.extensions = {{"ext.gstest.clock", "Test clock"}};
    return made;
}

const Json* itemWithId(const Json& list, std::string_view id)
{
    for (const Json& item : list) {
        if (same(item["id"], id)) {
            return &item;
        }
    }
    return nullptr;
}

}

TEST_CASE("ADR-0097 the system nodes of native extensions are blocks grouped under the extension that adds them")
{
    const Document document("log.info(1)\n", withClock(), {});
    REQUIRE(document.ok());
    const Json all = document.catalog("", "", "", "");
    const Json* now = itemWithId(all, "pure:ext.gstest.clock.now");
    const Json* ring = itemWithId(all, "call:ext.gstest.clock.ring");
    REQUIRE(now != nullptr);
    REQUIRE(ring != nullptr);
    CHECK((*now)["title"] == "now");
    CHECK((*now)["subtitle"] == "ext.gstest.clock.now");
    CHECK((*now)["group"] == "Test clock");
    CHECK((*ring)["title"] == "ring");
    CHECK((*ring)["group"] == "Test clock");
    CHECK((*ring)["icon"] == "plug");

    std::vector<std::string> found;
    for (const Json& item : document.catalog("", "", "", "test clock")) {
        found.push_back(item["id"].get<std::string>());
    }
    CHECK(found == std::vector<std::string>{"pure:ext.gstest.clock.now", "call:ext.gstest.clock.ring"});

    const Document plain("log.info(1)\n", environment(), {});
    CHECK(itemWithId(plain.catalog("", "", "", ""), "pure:ext.gstest.clock.now") == nullptr);
}
