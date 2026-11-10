#include "gs/input/KeyHints.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>

using namespace gs::input;

namespace {

Profile load(std::string_view body)
{
    const LoadResult result = loadProfile(R"({"format":"gamescrcpy.profile","version":3,)" + std::string(body) + "}");
    REQUIRE(result.ok());
    return result.profile;
}

Chord chord(std::string_view text)
{
    const auto parsed = parseChord(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

void expectNear(Vec2 actual, Vec2 expected)
{
    CHECK(actual.x == doctest::Approx(expected.x));
    CHECK(actual.y == doctest::Approx(expected.y));
}

}

TEST_CASE("a skill that follows the cursor carries its cast range around the hero (INP-022)")
{
    const Profile profile = load(R"("controls":[
        {"id":"skill","type":"skill","anchor":"bottomRight","offset":[-0.3,-0.2],
         "inputs":{"press":"KeyQ"},"direction":"cursor","castRadius":0.4,"hero":[0.1,0]},
        {"id":"drag","type":"skill","inputs":{"press":"KeyE","look":"Mouse.Move"}}])");
    const std::vector<KeyHint> hints = keyHints(profile, {.width = 2400, .height = 1080}, std::nullopt);
    REQUIRE(hints.size() == 2);
    REQUIRE(hints[0].castCenter.has_value());
    expectNear(*hints[0].castCenter, {0.5 + 0.1 * 1080.0 / 2400.0, 0.5});
    expectNear(hints[0].castExtent, {0.4 * 1080.0 / 2400.0, 0.4});
    CHECK_FALSE(hints[1].castCenter.has_value());
}

TEST_CASE("a direction wheel shows its ring and cells, a multi-press its places and a temporary cursor its key (ADR-0094)")
{
    const Profile profile = load(R"("controls":[
        {"id":"wheel","type":"dial","radius":0.2,"cells":6,"firstDeg":30,"inputs":{"press":"KeyQ","look":"Mouse.Move"}},
        {"id":"peekFire","type":"multiPress","points":[[0.1,0],[0,0.2]],"inputs":{"press":"KeyE"}},
        {"id":"pointer","type":"tempCursor","inputs":{"press":"AltLeft"}}])");
    const std::vector<KeyHint> hints = keyHints(profile, {.width = 2400, .height = 1080}, std::nullopt);
    REQUIRE(hints.size() == 3);
    CHECK(hints[0].type == "dial");
    expectNear(hints[0].extent, {0.2 * 1080.0 / 2400.0, 0.2});
    CHECK(hints[0].cells == 6);
    CHECK(hints[0].firstDeg == doctest::Approx(30.0));
    REQUIRE(hints[0].keys.size() == 1);
    CHECK(hints[0].keys[0].slot == "press");
    CHECK(hints[1].type == "multiPress");
    REQUIRE(hints[1].points.size() == 2);
    CHECK(hints[1].points[0].name == "points.0");
    CHECK(hints[1].points[1].name == "points.1");
    CHECK(hints[1].points[0].at.x > hints[1].center.x);
    CHECK(hints[1].points[1].at.y > hints[1].center.y);
    CHECK(hints[2].type == "tempCursor");
    REQUIRE(hints[2].keys.size() == 1);
    CHECK(hints[2].keys[0].input == chord("AltLeft"));
}

TEST_CASE("an action wheel shows its key, its cell count and a point for each cell that taps a spot (ADR-0094)")
{
    const Profile profile = load(R"("controls":[
        {"id":"items","type":"actionWheel","inputs":{"press":"KeyB"},
         "cells":[{"action":"logic"},{"action":"tap","at":[0.1,0]},{},{"action":"tap","at":[0,0.2]}]}])");
    const std::vector<KeyHint> hints = keyHints(profile, {.width = 2400, .height = 1080}, std::nullopt);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].type == "actionWheel");
    CHECK(hints[0].cells == 4);
    REQUIRE(hints[0].keys.size() == 1);
    CHECK(hints[0].keys[0].input == chord("KeyB"));
    REQUIRE(hints[0].points.size() == 2);
    CHECK(hints[0].points[0].name == "cells.1");
    CHECK(hints[0].points[1].name == "cells.3");
    CHECK(hints[0].points[0].at.x > hints[0].center.x);
    CHECK(hints[0].points[1].at.y > hints[0].center.y);
}

TEST_CASE("a right-click walk component shows its walk and stop keys on a ring (INP-022, ADR-0091)")
{
    const Profile profile = load(R"("controls":[
        {"id":"move","type":"rightWalk","inputs":{"walkTo":"Mouse.Right","stop":"KeyH"},"radius":0.2}])");
    const std::vector<KeyHint> hints = keyHints(profile, {.width = 2400, .height = 1080}, std::nullopt);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].type == "rightWalk");
    expectNear(hints[0].extent, {0.2 * 1080.0 / 2400.0, 0.2});
    REQUIRE(hints[0].keys.size() == 2);
    CHECK(hints[0].keys[0].slot == "walkTo");
    CHECK(hints[0].keys[0].input == chord("Mouse.Right"));
    CHECK(hints[0].keys[1].slot == "stop");
}

TEST_CASE("an instance of a custom component lists its keys by property name and its places in declared order (ADR-0091)")
{
    const Profile profile = load(R"("controls":[
        {"id":"turn","type":"component","component":"examples/in-turn","inputs":{"key":"KeyQ"},
         "props":{"second":[0.08,-0.04],"gapMs":120,"third":[0.16,0]}},
        {"id":"quiet","type":"component","component":"mine/quiet"}])");
    const ScreenGeometry screen{.width = 2400, .height = 1080};
    const std::vector<KeyHint> hints = keyHints(profile, screen, std::nullopt);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].type == "component");
    CHECK(hints[0].component == "examples/in-turn");
    expectNear(hints[0].center, {0.5, 0.5});
    REQUIRE(hints[0].keys.size() == 1);
    CHECK(hints[0].keys[0].slot == "key");
    CHECK(hints[0].keys[0].input == chord("KeyQ"));
    REQUIRE(hints[0].points.size() == 2);
    CHECK(hints[0].points[0].name == "second");
    expectNear(hints[0].points[0].at, {0.5 + 0.08 * 1080.0 / 2400.0, 0.5 - 0.04});
    CHECK(hints[0].points[1].name == "third");
    expectNear(hints[0].points[1].at, {0.5 + 0.16 * 1080.0 / 2400.0, 0.5});

    const std::vector<KeyHint> all = keyHints(profile, screen, std::nullopt, true);
    REQUIRE(all.size() == 2);
    CHECK(all[1].component == "mine/quiet");
    CHECK(all[1].keys.empty());
    CHECK(all[1].points.empty());
}

TEST_CASE("each component type names itself in its hint so the shape can follow it (ADR-0091)")
{
    const Profile profile = load(R"("controls":[
        {"id":"a","type":"tap","inputs":{"press":"KeyA"}},
        {"id":"b","type":"hold","inputs":{"press":"KeyB"}},
        {"id":"c","type":"longPress","inputs":{"press":"KeyC"}},
        {"id":"d","type":"turbo","inputs":{"press":"KeyD"}},
        {"id":"e","type":"padStick","inputs":{"stick":"Gamepad.LeftStick"}},
        {"id":"f","type":"padCamera","inputs":{"look":"Gamepad.RightStick"}}])");
    const std::vector<KeyHint> hints = keyHints(profile, {.width = 2400, .height = 1080}, std::nullopt);
    REQUIRE(hints.size() == 6);
    CHECK(hints[0].type == "tap");
    CHECK(hints[1].type == "hold");
    CHECK(hints[2].type == "longPress");
    CHECK(hints[3].type == "turbo");
    CHECK(hints[4].type == "padStick");
    CHECK(hints[4].keys[0].slot == "stick");
    CHECK(hints[5].type == "padCamera");
    CHECK(hints[5].keys[0].slot == "look");
}

TEST_CASE("controls without keys get no hint in play, but the editor can ask for every control")
{
    const Profile profile = load(R"("controls":[
        {"id":"fire","type":"hold","inputs":{"press":"KeyF"}},
        {"id":"spare","type":"tap"},
        {"id":"move","type":"wasd","radius":0.2}])");
    const ScreenGeometry screen{.width = 2400, .height = 1080};
    const std::vector<KeyHint> play = keyHints(profile, screen, std::nullopt);
    REQUIRE(play.size() == 1);
    CHECK(play[0].control == 0);
    const std::vector<KeyHint> every = keyHints(profile, screen, std::nullopt, true);
    REQUIRE(every.size() == 3);
    CHECK(every[1].control == 1);
    CHECK(every[1].keys.empty());
    expectNear(every[2].extent, {0.2 * 1080.0 / 2400.0, 0.2});
}

TEST_CASE("key hints sit where the controls touch and list the keys that drive them (INP-020)")
{
    const Profile profile = load(R"("controls":[
        {"id":"fire","type":"turbo","inputs":{"press":"Mouse.Left"}},
        {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],"radius":0.1,
         "inputs":{"up":"KeyW","down":"KeyS","left":"KeyA","right":"KeyD","sprint":"ShiftLeft"}},
        {"id":"look","type":"camera","size":[0.8,0.6],"inputs":{"look":"Mouse.Move"}},
        {"id":"spare","type":"tap"},
        {"id":"zoom","type":"pinch","anchor":"right","offset":[-0.2,0],
         "inputs":{"zoomIn":"Equal","zoomOut":"Minus"}}])");
    const std::vector<KeyHint> hints = keyHints(profile, {.width = 2000, .height = 1000}, chord("Backquote"));
    REQUIRE(hints.size() == 4);

    CHECK(hints[0].control == 0);
    CHECK(hints[0].type == "turbo");
    expectNear(hints[0].center, {0.5, 0.5});
    REQUIRE(hints[0].keys.size() == 1);
    CHECK(hints[0].keys[0].slot == "press");
    CHECK(hints[0].keys[0].input == chord("Mouse.Left"));

    CHECK(hints[1].type == "wasd");
    expectNear(hints[1].center, {0.15, 0.7});
    expectNear(hints[1].extent, {0.05, 0.1});
    REQUIRE(hints[1].keys.size() == 5);
    CHECK(hints[1].keys[0].slot == "up");
    CHECK(hints[1].keys[4].slot == "sprint");

    CHECK(hints[2].type == "camera");
    expectNear(hints[2].extent, {0.2, 0.3});
    REQUIRE(hints[2].keys.size() == 1);
    CHECK(hints[2].keys[0].slot == "lockKey");
    CHECK(hints[2].keys[0].input == chord("Backquote"));

    CHECK(hints[3].control == 4);
    expectNear(hints[3].center, {0.9, 0.5});
    REQUIRE(hints[3].keys.size() == 2);
    CHECK(hints[3].keys[1].slot == "zoomOut");
}

TEST_CASE("key hints follow the camera's own lock key and start out as the variables say (INP-020)")
{
    const Profile profile = load(R"("controls":[
        {"id":"look","type":"camera","lockKey":"F2","inputs":{"look":"Mouse.Move"}},
        {"id":"use","when":"bag","type":"tap","inputs":{"press":"KeyE"}},
        {"id":"walk","when":"!vehicle","type":"hold","inputs":{"press":"KeyF"}},
        {"id":"horn","enabled":false,"type":"tap","inputs":{"press":"KeyH"}},
        {"id":"score","when":"kills","type":"tap","inputs":{"press":"KeyK"}}],
        "vars":[{"id":"bag"},{"id":"vehicle","initial":true},{"id":"kills","initial":1}])");
    const std::vector<KeyHint> hints = keyHints(profile, {.width = 1600, .height = 900}, std::nullopt);
    REQUIRE(hints.size() == 5);
    CHECK(hints[0].keys[0].input == chord("F2"));
    CHECK(hints[0].live);
    CHECK_FALSE(hints[1].live);
    CHECK_FALSE(hints[2].live);
    CHECK_FALSE(hints[3].live);
    CHECK(hints[4].live);

    const Profile noLockKey = load(R"("controls":[{"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
    CHECK(keyHints(noLockKey, {.width = 1600, .height = 900}, std::nullopt).empty());
}
