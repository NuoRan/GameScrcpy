#include "gs/input/MappingEngine.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace gs::input;
using gs::Microseconds;
using gs::protocol::TouchAction;
using gs::protocol::TouchFrame;

namespace {

constexpr Microseconds at(long long milliseconds)
{
    return Microseconds{milliseconds * 1000};
}

InputSource sourceNamed(std::string_view name)
{
    const auto parsed = parseSource(name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

Chord chordNamed(std::string_view name)
{
    const auto parsed = parseChord(name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

Profile profileWith(std::string_view body)
{
    const LoadResult result = loadProfile(R"({"format":"gamescrcpy.profile","version":3,)" + std::string(body) + "}");
    REQUIRE(result.ok());
    return result.profile;
}

class Recorder final : public TouchFrameSink, public EngineListener
{
public:
    std::vector<TouchFrame> frames;
    std::vector<PointerState> pointerStates;
    std::vector<EngineNotice> notices;

    bool trySend(const TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }
    void pointerState(PointerState state) override { pointerStates.push_back(state); }
    void notice(EngineNotice notice) override { notices.push_back(notice); }
    void liveChanged(const std::vector<bool>&) override {}
};

struct Rig {
    Recorder recorder;
    MappingEngine engine;

    explicit Rig(const Profile& profile, std::optional<Chord> lockKey = std::nullopt)
        : engine(profile,
                 EngineConfig{.screen = {.width = 2400, .height = 1080},
                              .refreshHz = 60.0,
                              .pacingTick = Microseconds{0},
                              .defaultLockKey = lockKey},
                 recorder, recorder)
    {
    }
};

}

TEST_CASE("EDT-016: a key that starts closed stays out until switched on, and the open keys on one key all fire and conflict")
{
    const Profile profile = profileWith(R"("controls":[
        {"id":"fire","type":"hold","offset":[0.1,0],"inputs":{"press":"KeyF"}},
        {"id":"horn","type":"hold","offset":[0.2,0],"inputs":{"press":"KeyF"}},
        {"id":"flare","type":"hold","enabled":false,"offset":[0.3,0],"inputs":{"press":"KeyF"}}])");
    REQUIRE(profile.controls.size() == 3);
    CHECK_FALSE(profile.controls[2].enabled);

    const std::vector<InputConflict> conflicts = inputConflicts(profile);
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts[0].input == chordNamed("KeyF"));
    REQUIRE(conflicts[0].bindings.size() == 2);
    CHECK(conflicts[0].bindings[0].index != 2);
    CHECK(conflicts[0].bindings[1].index != 2);

    const LoadResult reloaded = loadProfile(saveProfile(profile));
    REQUIRE(reloaded.ok());
    CHECK(reloaded.profile == profile);

    Rig rig(profile);
    CHECK(rig.engine.controlEnabled("flare") == false);
    CHECK(rig.engine.controlLive("flare") == false);
    CHECK(rig.engine.controlLive("fire") == true);
    CHECK(rig.engine.controlLive("horn") == true);

    rig.engine.buttonDown(sourceNamed("KeyF"), at(0));
    CHECK(rig.engine.activeTouches() == 2);
    rig.engine.buttonUp(sourceNamed("KeyF"), at(50));
    CHECK(rig.engine.activeTouches() == 0);

    CHECK(rig.engine.setControlEnabled("flare", Switch::On, at(100)));
    CHECK(rig.engine.controlEnabled("flare") == true);
    CHECK(rig.engine.controlLive("flare") == true);
    rig.engine.buttonDown(sourceNamed("KeyF"), at(200));
    CHECK(rig.engine.activeTouches() == 3);
    rig.engine.buttonUp(sourceNamed("KeyF"), at(250));
    CHECK(rig.engine.activeTouches() == 0);

    CHECK(rig.engine.setControlEnabled("flare", Switch::Off, at(300)));
    rig.engine.buttonDown(sourceNamed("KeyF"), at(400));
    CHECK(rig.engine.activeTouches() == 2);
    rig.engine.buttonUp(sourceNamed("KeyF"), at(450));
    REQUIRE_FALSE(rig.recorder.frames.empty());
    CHECK(rig.recorder.frames.back().points.back().action == TouchAction::Up);
}

TEST_CASE("INP-015: a camera without its own lock key takes the program's default key, and mirroring starts in cursor state")
{
    const Profile profile = profileWith(R"("controls":[
        {"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}},
        {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
    const auto* camera = std::get_if<CameraControl>(&profile.controls.at(1).kind);
    REQUIRE(camera != nullptr);
    CHECK_FALSE(camera->lockKey.has_value());

    Rig rig(profile, chordNamed("Backquote"));
    CHECK(rig.engine.hasCamera());
    CHECK_FALSE(rig.engine.cameraLocked());
    rig.engine.buttonDown(sourceNamed("Mouse.Left"), at(0));
    CHECK(rig.engine.activeTouches() == 0);
    rig.engine.buttonUp(sourceNamed("Mouse.Left"), at(10));

    rig.engine.buttonDown(sourceNamed("Backquote"), at(20));
    rig.engine.buttonUp(sourceNamed("Backquote"), at(30));
    CHECK(rig.engine.cameraLocked());
    CHECK(rig.recorder.pointerStates == std::vector{PointerState::Captured});
    rig.engine.buttonDown(sourceNamed("Mouse.Left"), at(40));
    CHECK(rig.engine.activeTouches() == 1);
    rig.engine.buttonUp(sourceNamed("Mouse.Left"), at(50));
    CHECK(rig.engine.activeTouches() == 0);

    rig.engine.buttonDown(sourceNamed("Backquote"), at(60));
    rig.engine.buttonUp(sourceNamed("Backquote"), at(70));
    CHECK_FALSE(rig.engine.cameraLocked());
    CHECK(rig.recorder.pointerStates == std::vector{PointerState::Captured, PointerState::Free});
    CHECK(rig.recorder.notices.empty());

    Rig other(profile, chordNamed("F2"));
    other.engine.buttonDown(sourceNamed("Backquote"), at(0));
    other.engine.buttonUp(sourceNamed("Backquote"), at(10));
    CHECK_FALSE(other.engine.cameraLocked());
    other.engine.buttonDown(sourceNamed("F2"), at(20));
    other.engine.buttonUp(sourceNamed("F2"), at(30));
    CHECK(other.engine.cameraLocked());
}
