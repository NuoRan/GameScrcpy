#include "ExtensionHealth.h"

#include "unit/ext/TestFiles.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <string>

using namespace gs;
using namespace gs::ext::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

const std::chrono::sys_seconds kStart = std::chrono::sys_days{std::chrono::year{2026} / 11 / 11} + 7h;

app::HealthEvent crash(std::chrono::sys_seconds at, std::uint32_t code = 0xC0000005U, std::string report = {})
{
    return {.at = at, .version = "1.0.0", .kind = app::HealthKind::Crashed, .code = code, .report = std::move(report)};
}

}

TEST_CASE("ADR-0112: the health of an extension keeps what went wrong, and why it was turned off until it is turned on")
{
    Scratch scratch;
    const fs::path file = scratch / "extension-health.json";
    {
        app::ExtensionHealth health(file);
        CHECK(health.find("gstest.board") == nullptr);
        health.record("gstest.board", crash(kStart, 0xC0000005U, "gs-crash-20261111-070000-000"));
        REQUIRE(health.find("gstest.board") != nullptr);
        CHECK_FALSE(health.find("gstest.board")->off.has_value());
        health.record("gstest.board", {.at = kStart + 10s, .version = "1.0.0", .kind = app::HealthKind::Stopped});
        CHECK(health.find("gstest.board")->off == app::HealthKind::Stopped);
        CHECK(health.crashesSince("gstest.board", kStart) == 1);
        CHECK(health.crashesSince("gstest.board", kStart + 1s) == 0);
        CHECK(health.crashesSince("gstest.other", kStart) == 0);
    }
    app::ExtensionHealth again(file);
    const app::HealthRecord* record = again.find("gstest.board");
    REQUIRE(record != nullptr);
    REQUIRE(record->events.size() == 2);
    CHECK(record->events[0] == crash(kStart, 0xC0000005U, "gs-crash-20261111-070000-000"));
    CHECK(record->events[1].kind == app::HealthKind::Stopped);
    CHECK(record->off == app::HealthKind::Stopped);
    nlohmann::json text = nlohmann::json::parse(again.text());
    CHECK(text["format"] == "gamescrcpy.extension-health");
    CHECK(text["extensions"]["gstest.board"]["off"] == "stopped");
    CHECK(text["extensions"]["gstest.board"]["events"][0] ==
          nlohmann::json{{"at", "2026-11-11T07:00:00Z"},
                         {"version", "1.0.0"},
                         {"kind", "crashed"},
                         {"code", 0xC0000005U},
                         {"report", "gs-crash-20261111-070000-000"}});

    again.clear("gstest.board");
    REQUIRE(again.find("gstest.board") != nullptr);
    CHECK(again.find("gstest.board")->events.empty());
    CHECK(again.find("gstest.board")->off == app::HealthKind::Stopped);
    again.clearOff("gstest.board");
    CHECK(again.find("gstest.board") == nullptr);
    CHECK(app::ExtensionHealth(file).find("gstest.board") == nullptr);
}

TEST_CASE("ADR-0112: the health record keeps the last 20 events of an extension and the 64 extensions heard of last")
{
    app::ExtensionHealth health;
    for (int index = 0; index < 25; ++index) {
        health.record("gstest.busy", crash(kStart + std::chrono::seconds(index), static_cast<std::uint32_t>(index + 1)));
    }
    REQUIRE(health.find("gstest.busy")->events.size() == app::ExtensionHealth::kMostEvents);
    CHECK(health.find("gstest.busy")->events.front().code == 6);
    CHECK(health.find("gstest.busy")->events.back().code == 25);

    for (int index = 0; index < 64; ++index) {
        health.record("gstest.e" + std::to_string(index), crash(kStart + std::chrono::minutes(index + 1)));
    }
    CHECK(health.find("gstest.busy") == nullptr);
    CHECK(health.find("gstest.e0") != nullptr);
    health.record("gstest.late", crash(kStart - 1h));
    CHECK(health.find("gstest.late") != nullptr);
    CHECK(health.find("gstest.e0") == nullptr);
    CHECK(health.find("gstest.e63") != nullptr);
}

TEST_CASE("ADR-0112: a health record that GameScrcpy cannot read starts empty, and bad entries are left out")
{
    Scratch scratch;
    const fs::path file = scratch / "extension-health.json";
    writeFile(file, "not json");
    CHECK(app::ExtensionHealth(file).find("gstest.board") == nullptr);
    writeFile(file, R"({"format":"gamescrcpy.extension-health","version":2,"extensions":{"gstest.board":{"off":"stopped"}}})");
    CHECK(app::ExtensionHealth(file).find("gstest.board") == nullptr);
    writeFile(file, R"({"format":"gamescrcpy.extension-health","version":1,"extensions":{
        "gstest.board":{"off":"crashed","events":[
            {"at":"2026-11-11T07:00:00Z","version":"1.0.0","kind":"crashed","code":3221225477,"report":"..\\up"},
            {"at":"yesterday","kind":"crashed"},
            {"at":"2026-11-11T07:00:01Z","kind":"melted"},
            {"at":"2026-11-11T07:00:02Z","kind":"crashed","code":99999999999},
            "not an event"]},
        "gstest.quiet":{"events":[]},
        "gstest.off":{"off":"modulesFailed"}}})");
    const app::ExtensionHealth health(file);
    const app::HealthRecord* board = health.find("gstest.board");
    REQUIRE(board != nullptr);
    CHECK_FALSE(board->off.has_value());
    REQUIRE(board->events.size() == 2);
    CHECK(board->events[0].code == 0xC0000005U);
    CHECK(board->events[0].report.empty());
    CHECK(board->events[1].code == 0);
    CHECK(health.find("gstest.quiet") == nullptr);
    REQUIRE(health.find("gstest.off") != nullptr);
    CHECK(health.find("gstest.off")->off == app::HealthKind::ModulesFailed);
}
