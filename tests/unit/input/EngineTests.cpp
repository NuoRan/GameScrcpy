#include "gs/input/MappingEngine.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace gs::input;
using gs::Microseconds;
using gs::protocol::TouchAction;
using gs::protocol::TouchFrame;

namespace {

constexpr Microseconds ms(long long value)
{
    return Microseconds{value * 1000};
}

constexpr Microseconds kFrame{1'000'000 / 60};

InputSource source(std::string_view name)
{
    const auto parsed = parseSource(name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

class Recorder final : public TouchFrameSink, public EngineListener
{
public:
    std::vector<TouchFrame> frames;
    std::vector<PointerState> pointerStates;
    std::vector<EngineNotice> notices;
    std::vector<std::vector<bool>> lives;
    std::vector<std::vector<bool>> aimings;
    std::vector<std::pair<std::string, bool>> presses;
    std::vector<std::pair<std::string, bool>> stateChanges;
    std::vector<std::pair<std::string, bool>> enabledChanges;
    std::vector<bool> peeks;
    std::vector<std::tuple<std::string, bool, int>> wheels;

    bool trySend(const TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }
    void pointerState(PointerState state) override { pointerStates.push_back(state); }
    void notice(EngineNotice notice) override { notices.push_back(notice); }
    void liveChanged(const std::vector<bool>& controls) override { lives.push_back(controls); }
    void aimingChanged(const std::vector<bool>& controls) override { aimings.push_back(controls); }
    void controlPressed(std::string_view id, bool down) override { presses.emplace_back(id, down); }
    void stateChanged(std::string_view id, bool value) override { stateChanges.emplace_back(id, value); }
    void controlEnabledChanged(std::string_view id, bool on) override { enabledChanges.emplace_back(id, on); }
    void peekChanged(bool on) override { peeks.push_back(on); }
    void wheelChanged(std::string_view id, bool open, int cell) override { wheels.emplace_back(id, open, cell); }

    [[nodiscard]] TouchAction lastAction() const
    {
        REQUIRE_FALSE(frames.empty());
        return frames.back().points.back().action;
    }
};

struct Harness {
    Recorder recorder;
    MappingEngine engine;

    explicit Harness(std::string_view body, std::optional<Chord> lockKey = std::nullopt)
        : engine(load(body),
                 EngineConfig{.screen = {.width = 2400, .height = 1080},
                              .refreshHz = 60.0,
                              .pacingTick = Microseconds{0},
                              .defaultLockKey = lockKey},
                 recorder, recorder)
    {
    }

    static Profile load(std::string_view body)
    {
        const LoadResult result =
            loadProfile(R"({"format":"gamescrcpy.profile","version":3,)" + std::string(body) + "}");
        REQUIRE(result.ok());
        return result.profile;
    }
};

Chord chord(std::string_view text)
{
    const auto parsed = parseChord(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

gs::protocol::TouchPoint lastPoint(const Recorder& recorder)
{
    REQUIRE_FALSE(recorder.frames.empty());
    REQUIRE_FALSE(recorder.frames.back().points.empty());
    return recorder.frames.back().points.back();
}

}

TEST_CASE("a tap component taps once however long the key is held (ADR-0091)")
{
    Harness h(R"("controls":[{"id":"jump","type":"tap","offset":[0.3,0.2],"inputs":{"press":"Space"}}])");
    h.engine.buttonDown(source("Space"), ms(0));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    CHECK(h.engine.nextWakeup() == kFrame * 2);
    h.engine.tick(kFrame * 2);
    CHECK(h.recorder.lastAction() == TouchAction::Up);
    CHECK(h.engine.activeTouches() == 0);
    const std::size_t frames = h.recorder.frames.size();
    h.engine.buttonUp(source("Space"), ms(500));
    CHECK(h.recorder.frames.size() == frames);
    CHECK_FALSE(h.engine.nextWakeup().has_value());

    Harness slow(R"("controls":[{"id":"jump","type":"tap","inputs":{"press":"Space"},"tapMs":80}])");
    slow.engine.buttonDown(source("Space"), ms(0));
    slow.engine.buttonUp(source("Space"), ms(10));
    CHECK(slow.engine.activeTouches() == 1);
    CHECK(slow.engine.nextWakeup() == ms(80));
}

TEST_CASE("a hold component keeps its finger down exactly as long as the key (ADR-0091)")
{
    Harness h(R"("controls":[{"id":"fire","type":"hold","inputs":{"press":"KeyF"}}])");
    h.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    CHECK_FALSE(h.engine.nextWakeup().has_value());
    h.engine.tick(ms(5000));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.buttonUp(source("KeyF"), ms(5000));
    CHECK(h.recorder.lastAction() == TouchAction::Up);
}

TEST_CASE("a long press component holds for its set time from one press, whatever the key does (ADR-0091)")
{
    Harness h(R"("controls":[{"id":"bag","type":"longPress","inputs":{"press":"KeyB"},"holdMs":500}])");
    h.engine.buttonDown(source("KeyB"), ms(0));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    h.engine.buttonUp(source("KeyB"), ms(30));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(h.engine.nextWakeup() == ms(500));
    h.engine.tick(ms(499));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.tick(ms(500));
    CHECK(h.recorder.lastAction() == TouchAction::Up);
    CHECK_FALSE(h.engine.nextWakeup().has_value());

    Harness standard(R"("controls":[{"id":"bag","type":"longPress","inputs":{"press":"KeyB"}}])");
    standard.engine.buttonDown(source("KeyB"), ms(0));
    CHECK(standard.engine.nextWakeup() == ms(1000));
}

TEST_CASE("a turbo component taps at its rate while the key is held (ADR-0091)")
{
    Harness h(R"("controls":[{"id":"spray","type":"turbo","inputs":{"press":"KeyG"},"rateHz":10}])");
    h.engine.buttonDown(source("KeyG"), ms(0));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    std::size_t downs = 0;
    for (int at = 1; at <= 1000; ++at) {
        h.engine.tick(ms(at));
    }
    for (const TouchFrame& frame : h.recorder.frames) {
        for (const auto& point : frame.points) {
            downs += point.action == TouchAction::Down ? 1 : 0;
        }
    }
    CHECK(downs == 11);
    h.engine.buttonUp(source("KeyG"), ms(1001));
    h.engine.tick(ms(1100));
    CHECK(h.engine.activeTouches() == 0);
    CHECK_FALSE(h.engine.nextWakeup().has_value());
}

TEST_CASE("a component without a key is pressed only by the logic (ADR-0091)")
{
    using Events = std::vector<std::pair<std::string, bool>>;
    Harness h(R"("controls":[{"id":"loot","type":"hold","offset":[0.3,0.2]},
                             {"id":"jump","type":"hold","offset":[0.1,0.2],"inputs":{"press":"Space"}}])");
    CHECK_FALSE(h.engine.claims(source("KeyE")));
    CHECK(h.engine.pressControl("loot", true, ms(0)));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    CHECK(h.engine.pressControl("loot", false, ms(40)));
    CHECK(h.recorder.lastAction() == TouchAction::Up);
    CHECK(h.recorder.presses.empty());
    h.engine.buttonDown(source("Space"), ms(100));
    h.engine.buttonUp(source("Space"), ms(140));
    CHECK(h.recorder.presses == Events{{"jump", true}, {"jump", false}});
}

TEST_CASE("a key handed to the logic only tells the logic, and the logic can still press the component (issue 34)")
{
    using Events = std::vector<std::pair<std::string, bool>>;
    Harness h(R"("controls":[{"id":"loot","type":"hold","offset":[0.3,0.2],"inputs":{"press":"KeyF"}},
                             {"id":"spray","type":"turbo","inputs":{"press":"KeyG"},"rateHz":10},
                             {"id":"bag","type":"longPress","inputs":{"press":"KeyB"},"holdMs":500},
                             {"id":"move","type":"wasd","inputs":{"up":"KeyW"}}])");
    h.engine.setHandedOver({"loot", "spray", "bag", "move", "nope"});
    h.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(h.recorder.frames.empty());
    CHECK(h.engine.pressControl("loot", true, ms(10)));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    h.engine.buttonUp(source("KeyF"), ms(20));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(h.engine.pressControl("loot", false, ms(30)));
    CHECK(h.engine.activeTouches() == 0);
    CHECK(h.recorder.presses == Events{{"loot", true}, {"loot", false}});

    const std::size_t frames = h.recorder.frames.size();
    h.engine.buttonDown(source("KeyG"), ms(100));
    h.engine.buttonDown(source("KeyB"), ms(100));
    for (int at = 101; at <= 1200; ++at) {
        h.engine.tick(ms(at));
    }
    h.engine.buttonUp(source("KeyG"), ms(1201));
    h.engine.buttonUp(source("KeyB"), ms(1201));
    CHECK(h.recorder.frames.size() == frames);
    CHECK_FALSE(h.engine.nextWakeup().has_value());

    h.engine.buttonDown(source("KeyW"), ms(1300));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.buttonUp(source("KeyW"), ms(1400));

    h.engine.setHandedOver({});
    h.engine.buttonDown(source("KeyF"), ms(1500));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    h.engine.buttonUp(source("KeyF"), ms(1600));
    CHECK(h.recorder.lastAction() == TouchAction::Up);
}

TEST_CASE("handing a key over while it is held never leaves a finger down or lifts the logic's finger (issue 34)")
{
    Harness h(R"("controls":[{"id":"loot","type":"hold","offset":[0.3,0.2],"inputs":{"press":"KeyF"}}])");
    h.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.setHandedOver({"loot"});
    h.engine.buttonUp(source("KeyF"), ms(50));
    CHECK(h.engine.activeTouches() == 0);

    h.engine.buttonDown(source("KeyF"), ms(100));
    CHECK(h.engine.pressControl("loot", true, ms(110)));
    h.engine.setHandedOver({});
    h.engine.buttonUp(source("KeyF"), ms(150));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(h.engine.pressControl("loot", false, ms(200)));
    CHECK(h.engine.activeTouches() == 0);

    h.engine.setHandedOver({"loot"});
    h.engine.buttonDown(source("KeyF"), ms(300));
    h.engine.resetAll(ms(310));
    h.engine.setHandedOver({});
    h.engine.buttonDown(source("KeyF"), ms(400));
    CHECK(h.engine.activeTouches() == 1);
}

TEST_CASE("variables switch controls off, release their fingers and switch them back on (ADR-0070)")
{
    Harness h(R"("controls":[{"id":"fire","when":"!bag","type":"hold","offset":[0.3,0.2],"inputs":{"press":"Mouse.Left"}},
                             {"id":"use","when":"bag","type":"hold","inputs":{"press":"KeyE"}},
                             {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}],
                 "vars":[{"id":"bag"}])");
    REQUIRE(h.recorder.lives.size() == 1);
    CHECK(h.recorder.lives.back() == std::vector<bool>{true, false, true});
    h.engine.setCameraLock(Switch::On, ms(0));
    h.engine.buttonDown(source("Mouse.Left"), ms(0));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(h.engine.controlLive("use") == false);

    CHECK(h.engine.setState("bag", Switch::Toggle, ms(100)));
    CHECK(h.engine.state("bag") == true);
    CHECK(h.recorder.lastAction() == TouchAction::Cancel);
    CHECK(h.engine.activeTouches() == 0);
    CHECK(h.recorder.lives.back() == std::vector<bool>{false, true, true});
    CHECK(h.recorder.lives.size() == 2);

    const std::size_t framesBefore = h.recorder.frames.size();
    h.engine.buttonUp(source("Mouse.Left"), ms(200));
    h.engine.buttonDown(source("Mouse.Left"), ms(250));
    CHECK(h.recorder.frames.size() == framesBefore);
    h.engine.buttonUp(source("Mouse.Left"), ms(260));

    h.engine.buttonDown(source("KeyE"), ms(300));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    h.engine.buttonUp(source("KeyE"), ms(350));

    CHECK(h.engine.setState("bag", Switch::Toggle, ms(400)));
    CHECK(h.engine.state("bag") == false);
    h.engine.buttonDown(source("Mouse.Left"), ms(500));
    CHECK(h.engine.activeTouches() == 1);
    CHECK_FALSE(h.engine.setState("ghost", Switch::On, ms(600)));
    CHECK_FALSE(h.engine.setControlEnabled("ghost", Switch::Off, ms(600)));
}

TEST_CASE("a number or text variable counts as on when it is not zero or empty")
{
    Harness h(R"("controls":[{"id":"a","when":"kills","type":"hold","inputs":{"press":"KeyA"}},
                             {"id":"b","when":"!mode","type":"hold","inputs":{"press":"KeyB"}}],
                 "vars":[{"id":"kills","initial":2},{"id":"mode","initial":"ranked"}])");
    CHECK(h.engine.controlLive("a") == true);
    CHECK(h.engine.controlLive("b") == false);
}

TEST_CASE("one key drives every live binding and modifiers pick the more specific ones (ADR-0070)")
{
    Harness h(R"("controls":[{"id":"plain","type":"hold","offset":[0.1,0],"inputs":{"press":"KeyF"}},
                             {"id":"ctrl","type":"hold","offset":[0.2,0],"inputs":{"press":"Ctrl+KeyF"}},
                             {"id":"horn","type":"hold","offset":[0.3,0],"inputs":{"press":"KeyF"}}])");
    h.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(h.engine.activeTouches() == 2);
    h.engine.buttonUp(source("KeyF"), ms(100));
    CHECK(h.engine.activeTouches() == 0);

    h.engine.buttonDown(source("ControlLeft"), ms(200));
    h.engine.buttonDown(source("KeyF"), ms(210));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.buttonUp(source("KeyF"), ms(300));
    h.engine.buttonUp(source("ControlLeft"), ms(310));
    CHECK(h.engine.activeTouches() == 0);
}

TEST_CASE("mouse motion goes to free look while it is held and to the camera otherwise")
{
    Harness h(R"("controls":[{"id":"look","type":"camera","anchor":"right","offset":[-0.5,0],"inputs":{"look":"Mouse.Move"}},
                             {"id":"peek","type":"freeLook","anchor":"topRight","offset":[-0.2,0.2],
                              "inputs":{"hold":"Mouse.Right","look":"Mouse.Move"}}],
                 "screen":{"scaleMode":"shortSide"})");
    h.engine.setCameraLock(Switch::On, ms(0));
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(0));
    REQUIRE(h.recorder.frames.size() == 2);
    const std::uint8_t cameraFinger = h.recorder.frames[0].points[0].pointerId;

    h.engine.buttonDown(source("Mouse.Right"), ms(10));
    const std::uint8_t peekFinger = h.recorder.frames.back().points.back().pointerId;
    CHECK(peekFinger != cameraFinger);
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(20));
    CHECK(h.recorder.frames.back().points.back().pointerId == peekFinger);

    h.engine.buttonUp(source("Mouse.Right"), ms(30));
    CHECK(h.recorder.lastAction() == TouchAction::Up);
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(40));
    CHECK(h.recorder.frames.back().points.back().pointerId == cameraFinger);
}

TEST_CASE("mouse motion goes to the held control pressed last, so a wheel turns while free look is held (inbox 9)")
{
    Harness h(R"("controls":[{"id":"look","type":"camera","anchor":"right","offset":[-0.5,0],"inputs":{"look":"Mouse.Move"}},
                             {"id":"peek","type":"freeLook","anchor":"topRight","offset":[-0.2,0.2],
                              "inputs":{"hold":"AltLeft","look":"Mouse.Move"}},
                             {"id":"heal","type":"dial","radius":0.1,"cells":4,"inputs":{"press":"KeyQ","look":"Mouse.Move"}}],
                 "screen":{"scaleMode":"shortSide"})");
    h.engine.setCameraLock(Switch::On, ms(0));
    h.engine.buttonDown(source("AltLeft"), ms(0));
    const std::uint8_t peekFinger = lastPoint(h.recorder).pointerId;
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(10));
    CHECK(lastPoint(h.recorder).pointerId == peekFinger);
    CHECK(lastPoint(h.recorder).action == TouchAction::Move);

    h.engine.buttonDown(source("KeyQ"), ms(20));
    const std::uint8_t healFinger = lastPoint(h.recorder).pointerId;
    CHECK(healFinger != peekFinger);
    h.engine.motion(source("Mouse.Move"), 30, 0, ms(30));
    CHECK(lastPoint(h.recorder).pointerId == healFinger);
    CHECK(lastPoint(h.recorder).action == TouchAction::Move);
    CHECK(h.engine.controlStatus("heal")->cell >= 0);

    h.engine.buttonUp(source("KeyQ"), ms(40));
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(50));
    CHECK(lastPoint(h.recorder).pointerId == peekFinger);
    CHECK(lastPoint(h.recorder).action == TouchAction::Move);

    h.engine.buttonUp(source("AltLeft"), ms(60));
    CHECK(h.engine.activeTouches() == 0);
}

TEST_CASE("resetAll releases every finger and forgets held inputs but keeps the lock (MAP-011)")
{
    Harness h(R"("controls":[{"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}},
                             {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],
                              "inputs":{"up":"KeyW","left":"KeyA"}},
                             {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
    h.engine.setCameraLock(Switch::On, ms(0));
    h.engine.buttonDown(source("Mouse.Left"), ms(0));
    h.engine.buttonDown(source("KeyW"), ms(0));
    h.engine.buttonDown(source("KeyA"), ms(0));
    h.engine.motion(source("Mouse.Move"), 300, 100, ms(0));
    CHECK(h.engine.activeTouches() == 3);

    h.engine.resetAll(ms(10));
    CHECK(h.engine.activeTouches() == 0);
    CHECK(h.engine.cameraLocked());
    for (const auto& point : h.recorder.frames.back().points) {
        CHECK(point.action == TouchAction::Cancel);
    }
    const std::size_t framesAfterReset = h.recorder.frames.size();
    h.engine.buttonUp(source("KeyW"), ms(20));
    h.engine.buttonUp(source("Mouse.Left"), ms(20));
    CHECK(h.recorder.frames.size() == framesAfterReset);
    CHECK_FALSE(h.engine.nextWakeup().has_value());
}

TEST_CASE("the camera's lock key toggles the lock and mouse bindings only act while locked (ADR-0070)")
{
    Harness h(R"("controls":[{"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}},
                             {"id":"look","type":"camera","lockKey":"Backquote","inputs":{"look":"Mouse.Move"}}])",
              chord("F1"));
    h.engine.buttonDown(source("Mouse.Left"), ms(0));
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(0));
    h.engine.buttonUp(source("Mouse.Left"), ms(10));
    CHECK(h.recorder.frames.empty());

    h.engine.buttonDown(source("F1"), ms(20));
    h.engine.buttonUp(source("F1"), ms(30));
    CHECK_FALSE(h.engine.cameraLocked());

    h.engine.buttonDown(source("Backquote"), ms(40));
    h.engine.buttonUp(source("Backquote"), ms(50));
    CHECK(h.engine.cameraLocked());
    CHECK(h.recorder.pointerStates == std::vector{PointerState::Captured});

    h.engine.buttonDown(source("Mouse.Left"), ms(100));
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(110));
    CHECK(h.engine.activeTouches() == 2);

    h.engine.buttonDown(source("Backquote"), ms(200));
    CHECK_FALSE(h.engine.cameraLocked());
    CHECK(h.recorder.pointerStates == std::vector{PointerState::Captured, PointerState::Free});
    CHECK(h.engine.activeTouches() == 0);
    h.engine.buttonUp(source("Mouse.Left"), ms(210));
    CHECK(h.engine.activeTouches() == 0);
}

TEST_CASE("without a camera control the default lock key only reports that there is none (ADR-0070)")
{
    Harness h(R"("controls":[{"id":"fire","type":"hold","inputs":{"press":"Space"}},
                             {"id":"pad","type":"padCamera","inputs":{"look":"Gamepad.RightStick"}}])",
              chord("Backquote"));
    h.engine.buttonDown(source("Backquote"), ms(0));
    h.engine.buttonUp(source("Backquote"), ms(10));
    CHECK_FALSE(h.engine.cameraLocked());
    CHECK_FALSE(h.engine.hasCamera());
    CHECK(h.recorder.notices == std::vector{EngineNotice::NoCamera});
    CHECK(h.recorder.pointerStates.empty());
    h.engine.setCameraLock(Switch::On, ms(20));
    CHECK(h.recorder.notices.size() == 2);
}

TEST_CASE("the engine asks for no wakeup while idle (R14)")
{
    Harness h(R"("controls":[{"id":"fire","type":"turbo","inputs":{"press":"KeyG"},"rateHz":10},
                             {"id":"long","type":"longPress","inputs":{"press":"KeyL"},"holdMs":300}])");
    CHECK_FALSE(h.engine.nextWakeup().has_value());
    h.engine.buttonDown(source("KeyL"), ms(0));
    CHECK(h.engine.nextWakeup() == ms(300));
    h.engine.buttonUp(source("KeyL"), ms(100));
    h.engine.tick(ms(300));
    CHECK_FALSE(h.engine.nextWakeup().has_value());

    h.engine.buttonDown(source("KeyG"), ms(1000));
    CHECK(h.engine.nextWakeup() == ms(1000) + kFrame * 2);
    h.engine.buttonUp(source("KeyG"), ms(1010));
    h.engine.tick(ms(1034));
    CHECK_FALSE(h.engine.nextWakeup().has_value());
}

TEST_CASE("the cursor touches with its own pointer from the shared allocator and resetAll cancels it (ADR-0069)")
{
    Harness h(R"("controls":[{"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Space"}}])");
    h.engine.cursor(CursorPhase::Move, {0.1, 0.1}, ms(0));
    h.engine.cursor(CursorPhase::Up, {0.1, 0.1}, ms(0));
    CHECK(h.recorder.frames.empty());

    h.engine.cursor(CursorPhase::Down, {0.25, 0.5}, ms(10));
    REQUIRE(h.recorder.frames.size() == 1);
    const auto cursor = h.recorder.frames.back().points.back();
    CHECK(cursor.action == TouchAction::Down);
    CHECK(cursor.x == 16384);
    CHECK(cursor.y == 32768);

    h.engine.buttonDown(source("Space"), ms(20));
    REQUIRE(h.recorder.frames.size() == 2);
    const auto fire = h.recorder.frames.back().points.back();
    CHECK(fire.action == TouchAction::Down);
    CHECK(fire.pointerId != cursor.pointerId);
    CHECK(h.engine.activeTouches() == 2);

    h.engine.cursor(CursorPhase::Move, {0.5, 0.5}, ms(30));
    CHECK(h.recorder.lastAction() == TouchAction::Move);
    CHECK(h.recorder.frames.back().points.back().pointerId == cursor.pointerId);
    CHECK(h.recorder.frames.back().points.back().x == 32768);

    h.engine.cursor(CursorPhase::Up, {0.75, 0.5}, ms(40));
    CHECK(h.recorder.lastAction() == TouchAction::Up);
    CHECK(h.recorder.frames.back().points.back().x == 49151);
    CHECK(h.engine.activeTouches() == 1);

    h.engine.cursor(CursorPhase::Down, {0.5, 0.25}, ms(50));
    const auto again = h.recorder.frames.back().points.back();
    CHECK(again.action == TouchAction::Down);
    CHECK(again.pointerId != cursor.pointerId);
    CHECK(again.pointerId != fire.pointerId);

    h.engine.resetAll(ms(60));
    CHECK(h.engine.activeTouches() == 0);
    const auto& last = h.recorder.frames.back().points;
    const bool cancelled = std::any_of(last.begin(), last.end(), [&](const gs::protocol::TouchPoint& point) {
        return point.pointerId == again.pointerId && point.action == TouchAction::Cancel && point.y == 16384;
    });
    CHECK(cancelled);
    h.engine.cursor(CursorPhase::Up, {0.5, 0.25}, ms(70));
    CHECK(h.engine.activeTouches() == 0);
}

namespace {

constexpr std::string_view kSkill = R"("controls":[{"id":"skill","type":"skill","inputs":{"press":"KeyQ","cancel":"Space"},
    "direction":"cursor","cast":"%CAST%"%MORE%}])";

std::string skill(std::string_view cast, std::string_view more = {})
{
    std::string body(kSkill);
    body.replace(body.find("%CAST%"), 6, cast);
    body.replace(body.find("%MORE%"), 6, more);
    return body;
}

}

TEST_CASE("a cursor skill drags toward the cursor, at most to the wheel edge, and casts on release (INP-022)")
{
    Harness h(skill("release"));
    std::optional<Vec2> cursor = Vec2{0.9, 0.5};
    h.engine.setCursorLocator([&cursor] { return cursor; });

    h.engine.buttonDown(source("KeyQ"), ms(0));
    REQUIRE(h.recorder.frames.size() == 2);
    CHECK(h.recorder.frames[0].points.back().action == TouchAction::Down);
    CHECK(h.recorder.frames[0].points.back().x == 32768);
    const auto aimed = lastPoint(h.recorder);
    CHECK(aimed.action == TouchAction::Move);
    CHECK(aimed.x == 37191);
    CHECK(aimed.y == 32768);

    cursor = Vec2{0.5, 0.5 - 0.175};
    h.engine.motion(source("Mouse.Move"), 0, -5, ms(20));
    CHECK(lastPoint(h.recorder).action == TouchAction::Move);
    CHECK(lastPoint(h.recorder).x == 32768);
    CHECK(lastPoint(h.recorder).y == 27852);

    h.engine.buttonUp(source("KeyQ"), ms(40));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(lastPoint(h.recorder).y == 27852);
    CHECK(h.engine.activeTouches() == 0);
    CHECK(h.recorder.aimings == std::vector<std::vector<bool>>{{true}, {false}});
}

TEST_CASE("a skill set to cast on press lands, aims and lifts at once, and a manual one waits for a click (INP-022)")
{
    SUBCASE("press")
    {
        Harness h(skill("press"));
        h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.1, 0.5}); });
        h.engine.buttonDown(source("KeyQ"), ms(0));
        CHECK(lastPoint(h.recorder).action == TouchAction::Up);
        CHECK(lastPoint(h.recorder).x == 28344);
        CHECK(h.engine.activeTouches() == 0);
        h.engine.buttonUp(source("KeyQ"), ms(30));
        CHECK(h.engine.activeTouches() == 0);
    }
    SUBCASE("manual")
    {
        Harness h(skill("manual"));
        h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.5, 0.9}); });
        h.engine.buttonDown(source("KeyQ"), ms(0));
        h.engine.buttonUp(source("KeyQ"), ms(30));
        CHECK(h.engine.activeTouches() == 1);
        CHECK(lastPoint(h.recorder).action == TouchAction::Move);
        const std::size_t frames = h.recorder.frames.size();
        h.engine.cursor(CursorPhase::Down, {0.5, 0.9}, ms(60));
        CHECK(h.recorder.frames.size() == frames + 1);
        CHECK(lastPoint(h.recorder).action == TouchAction::Up);
        CHECK(lastPoint(h.recorder).y == 42598);
        h.engine.cursor(CursorPhase::Up, {0.5, 0.9}, ms(80));
        CHECK(h.engine.activeTouches() == 0);
        CHECK(h.recorder.frames.size() == frames + 1);

        h.engine.buttonDown(source("KeyQ"), ms(100));
        h.engine.buttonUp(source("KeyQ"), ms(120));
        h.engine.buttonDown(source("KeyQ"), ms(140));
        CHECK(lastPoint(h.recorder).action == TouchAction::Up);
        CHECK(h.engine.activeTouches() == 0);
    }
}

TEST_CASE("the cancel key lifts on the game's cancel button, or cancels the touch when none is set (INP-022)")
{
    SUBCASE("cancel button")
    {
        Harness h(skill("release", R"(,"cancelAt":[0,-0.4])"));
        h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.9, 0.5}); });
        h.engine.buttonDown(source("KeyQ"), ms(0));
        h.engine.buttonDown(source("Space"), ms(20));
        CHECK(lastPoint(h.recorder).action == TouchAction::Move);
        CHECK(lastPoint(h.recorder).y == 6553);
        h.engine.buttonUp(source("KeyQ"), ms(25));
        CHECK(h.engine.activeTouches() == 1);
        h.engine.tick(ms(40));
        CHECK(lastPoint(h.recorder).action == TouchAction::Up);
        CHECK(lastPoint(h.recorder).y == 6553);
        CHECK(h.engine.activeTouches() == 0);
    }
    SUBCASE("no cancel button")
    {
        Harness h(skill("release"));
        h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.9, 0.5}); });
        h.engine.buttonDown(source("KeyQ"), ms(0));
        h.engine.buttonDown(source("Space"), ms(20));
        CHECK(lastPoint(h.recorder).action == TouchAction::Cancel);
        CHECK(h.engine.activeTouches() == 0);
        h.engine.buttonUp(source("KeyQ"), ms(30));
        CHECK(h.engine.activeTouches() == 0);
    }
}

TEST_CASE("a skill that pauses the walking stick holds it still while aiming (INP-022)")
{
    Harness h(R"("controls":[
        {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],
         "inputs":{"up":"KeyW","right":"KeyD"}},
        {"id":"skill","type":"skill","inputs":{"press":"KeyQ"},"direction":"cursor","pauses":"move"}])");
    h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.9, 0.5}); });
    h.engine.buttonDown(source("KeyW"), ms(0));
    const std::uint8_t stick = lastPoint(h.recorder).pointerId;
    const auto walking = lastPoint(h.recorder);
    h.engine.buttonDown(source("KeyQ"), ms(20));
    const std::size_t frames = h.recorder.frames.size();
    h.engine.buttonDown(source("KeyD"), ms(30));
    CHECK(h.recorder.frames.size() == frames);
    h.engine.buttonUp(source("KeyQ"), ms(40));
    bool moved = false;
    for (std::size_t i = frames; i < h.recorder.frames.size(); ++i) {
        for (const auto& point : h.recorder.frames[i].points) {
            moved = moved || (point.pointerId == stick && point.action == TouchAction::Move && point.x > walking.x);
        }
    }
    CHECK(moved);
}

TEST_CASE("gamepad sticks drive the gamepad stick and gamepad view components without a lock (INP-023, ADR-0091)")
{
    Harness h(R"("controls":[
        {"id":"move","type":"padStick","anchor":"bottomLeft","offset":[0.3,-0.3],"inputs":{"stick":"Gamepad.LeftStick"}},
        {"id":"look","type":"padCamera","anchor":"right","offset":[-0.4,0],"inputs":{"look":"Gamepad.RightStick"}}])");
    h.engine.axis(source("Gamepad.LeftStick"), {1.0, 0.0}, ms(0));
    const auto walking = lastPoint(h.recorder);
    CHECK(walking.action == TouchAction::Move);
    const std::uint8_t stick = walking.pointerId;
    h.engine.axis(source("Gamepad.LeftStick"), {0.0, 0.0}, ms(50));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(lastPoint(h.recorder).pointerId == stick);

    CHECK_FALSE(h.engine.cameraLocked());
    h.engine.axis(source("Gamepad.RightStick"), {1.0, 0.0}, ms(100));
    const auto landed = lastPoint(h.recorder);
    CHECK(landed.action == TouchAction::Down);
    REQUIRE(h.engine.nextWakeup().has_value());
    CHECK(*h.engine.nextWakeup() == ms(100) + Microseconds{16'666});
    h.engine.tick(ms(100) + Microseconds{16'666});
    h.engine.tick(ms(100) + Microseconds{33'332});
    const auto turned = lastPoint(h.recorder);
    CHECK(turned.action == TouchAction::Move);
    CHECK(turned.pointerId == landed.pointerId);
    CHECK(turned.x > landed.x);
    CHECK(turned.y == landed.y);

    h.engine.axis(source("Gamepad.RightStick"), {0.0, 0.0}, ms(200));
    REQUIRE(h.engine.nextWakeup().has_value());
    CHECK(*h.engine.nextWakeup() > ms(1000));
}

TEST_CASE("a gamepad stick nothing names drives nothing, not even the WASD stick or the view (ADR-0091)")
{
    Harness h(R"("controls":[
        {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],"inputs":{"up":"KeyW"}},
        {"id":"look","type":"camera","anchor":"right","offset":[-0.4,0],"inputs":{"look":"Mouse.Move"}},
        {"id":"pad","type":"padStick","inputs":{"stick":"Gamepad.RightStick"}}])");
    h.engine.axis(source("Gamepad.LeftStick"), {1.0, 0.0}, ms(0));
    CHECK(h.recorder.frames.empty());
    h.engine.axis(source("Gamepad.RightStick"), {1.0, 0.0}, ms(10));
    REQUIRE(h.engine.activeTouches() == 1);
    const auto first = lastPoint(h.recorder);
    CHECK(first.action == TouchAction::Move);
    h.engine.axis(source("Gamepad.LeftStick"), {0.0, 1.0}, ms(120));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(lastPoint(h.recorder).pointerId == first.pointerId);
    CHECK(lastPoint(h.recorder).x == first.x);
}

TEST_CASE("right-click walking heads for the cursor, finishes a click on time and stops on release or Stop (INP-022)")
{
    Harness h(R"("controls":[{"id":"move","type":"rightWalk","inputs":{"walkTo":"Mouse.Right","stop":"KeyS"},
                              "walkMsPerUnit":600}])");
    std::optional<Vec2> cursor = Vec2{0.9, 0.5};
    h.engine.setCursorLocator([&cursor] { return cursor; });
    CHECK_FALSE(h.engine.cameraLocked());

    h.engine.buttonDown(source("Mouse.Right"), ms(0));
    CHECK(lastPoint(h.recorder).action == TouchAction::Move);
    CHECK(lastPoint(h.recorder).x > 32768);
    CHECK(lastPoint(h.recorder).y == 32768);
    h.engine.buttonUp(source("Mouse.Right"), ms(100));
    CHECK(h.engine.activeTouches() == 1);
    REQUIRE(h.engine.nextWakeup().has_value());
    CHECK(*h.engine.nextWakeup() == Microseconds{533'333});
    h.engine.tick(Microseconds{533'333});
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(h.engine.activeTouches() == 0);

    h.engine.buttonDown(source("Mouse.Right"), ms(1000));
    cursor = Vec2{0.5, 0.9};
    h.engine.motion(source("Mouse.Move"), 0, 10, ms(1100));
    CHECK(lastPoint(h.recorder).x == 32768);
    CHECK(lastPoint(h.recorder).y > 32768);
    h.engine.buttonUp(source("Mouse.Right"), ms(1500));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);

    h.engine.buttonDown(source("Mouse.Right"), ms(2000));
    h.engine.buttonUp(source("Mouse.Right"), ms(2050));
    h.engine.buttonDown(source("KeyS"), ms(2100));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    h.engine.buttonUp(source("KeyS"), ms(2120));
}

TEST_CASE("right-click walking measures from the hero position it is given (ADR-0091)")
{
    Harness h(R"("controls":[{"id":"move","type":"rightWalk","inputs":{"walkTo":"Mouse.Right"},"hero":[0.4,0]}])");
    h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.6, 0.5}); });
    h.engine.buttonDown(source("Mouse.Right"), ms(0));
    CHECK(lastPoint(h.recorder).action == TouchAction::Move);
    CHECK(lastPoint(h.recorder).x < 32768);
}

TEST_CASE("a mouse binding answers only in the lock states its claim names (ADR-0077)")
{
    Harness h(R"("controls":[
        {"id":"scope","type":"hold","inputs":{"press":"Mouse.Right"}},
        {"id":"menu","type":"hold","offset":[0.2,0],"inputs":{"press":{"input":"Mouse.Middle","claim":"cursor"}}},
        {"id":"jump","type":"hold","offset":[0.4,0],"inputs":{"press":{"input":"Mouse.X1","claim":"both"}}},
        {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
    const auto fires = [&](std::string_view name, long long at) {
        const std::uint8_t before = h.engine.activeTouches();
        h.engine.buttonDown(source(name), ms(at));
        const bool fired = h.engine.activeTouches() > before;
        h.engine.buttonUp(source(name), ms(at + 10));
        return fired;
    };

    CHECK_FALSE(h.engine.cameraLocked());
    CHECK_FALSE(fires("Mouse.Right", 0));
    CHECK(fires("Mouse.Middle", 100));
    CHECK(fires("Mouse.X1", 200));
    CHECK(h.engine.claims(source("Mouse.Middle")));
    CHECK_FALSE(h.engine.claims(source("Mouse.Right")));
    CHECK_FALSE(h.engine.claims(source("Mouse.WheelUp")));

    h.engine.setCameraLock(Switch::On, ms(400));
    CHECK(fires("Mouse.Right", 500));
    CHECK_FALSE(fires("Mouse.Middle", 600));
    CHECK(fires("Mouse.X1", 700));
    CHECK(h.engine.claims(source("Mouse.Right")));
    CHECK_FALSE(h.engine.claims(source("Mouse.Middle")));

    h.engine.setCameraLock(Switch::Off, ms(900));
    h.engine.buttonDown(source("Mouse.Middle"), ms(1000));
    const std::uint8_t menu = lastPoint(h.recorder).pointerId;
    CHECK(lastPoint(h.recorder).action == TouchAction::Down);
    h.engine.setCameraLock(Switch::On, ms(1100));
    bool lifted = false;
    for (const auto& frame : h.recorder.frames) {
        for (const auto& point : frame.points) {
            lifted = lifted || (point.pointerId == menu && point.action == TouchAction::Up);
        }
    }
    CHECK(lifted);
    h.engine.buttonUp(source("Mouse.Middle"), ms(1200));
}

TEST_CASE("right-click walking answers in cursor state by default and follows an explicit claim (ADR-0077)")
{
    Harness h(R"("controls":[
        {"id":"move","type":"rightWalk","inputs":{"walkTo":"Mouse.Right"}},
        {"id":"other","type":"rightWalk","offset":[0.3,0],"inputs":{"walkTo":{"input":"Mouse.X1","claim":"lock"}}},
        {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
    h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.9, 0.5}); });

    h.engine.buttonDown(source("Mouse.Right"), ms(0));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.resetAll(ms(10));
    h.engine.buttonDown(source("Mouse.X1"), ms(20));
    CHECK(h.engine.activeTouches() == 0);
    h.engine.buttonUp(source("Mouse.X1"), ms(30));

    h.engine.setCameraLock(Switch::On, ms(40));
    const std::uint8_t locked = h.engine.activeTouches();
    h.engine.buttonDown(source("Mouse.Right"), ms(50));
    CHECK(h.engine.activeTouches() == locked);
    h.engine.buttonUp(source("Mouse.Right"), ms(60));
    h.engine.buttonDown(source("Mouse.X1"), ms(70));
    CHECK(h.engine.activeTouches() == locked + 1);
    h.engine.buttonUp(source("Mouse.X1"), ms(80));
}

TEST_CASE("a manual skill casts with its cast key, only while it aims (ADR-0077)")
{
    SUBCASE("a key")
    {
        Harness h(R"("controls":[{"id":"skill","type":"skill","inputs":{"press":"KeyQ","cast":"KeyF"},
                                  "direction":"cursor","cast":"manual"}])");
        h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.5, 0.9}); });
        h.engine.buttonDown(source("KeyF"), ms(0));
        h.engine.buttonUp(source("KeyF"), ms(5));
        CHECK(h.recorder.frames.empty());

        h.engine.buttonDown(source("KeyQ"), ms(10));
        h.engine.buttonUp(source("KeyQ"), ms(30));
        CHECK(h.engine.activeTouches() == 1);
        h.engine.cursor(CursorPhase::Down, {0.2, 0.2}, ms(40));
        CHECK(h.engine.activeTouches() == 2);
        h.engine.cursor(CursorPhase::Up, {0.2, 0.2}, ms(50));
        CHECK(h.engine.activeTouches() == 1);

        h.engine.buttonDown(source("KeyF"), ms(60));
        CHECK(lastPoint(h.recorder).action == TouchAction::Up);
        CHECK(lastPoint(h.recorder).y == 42598);
        CHECK(h.engine.activeTouches() == 0);
        h.engine.buttonUp(source("KeyF"), ms(70));
    }
    SUBCASE("the right button while locked")
    {
        Harness h(R"("controls":[{"id":"skill","type":"skill","inputs":{"press":"KeyQ",
                                  "cast":{"input":"Mouse.Right","claim":"lock"}},"cast":"manual"},
                                 {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
        h.engine.buttonDown(source("KeyQ"), ms(0));
        h.engine.buttonUp(source("KeyQ"), ms(20));
        h.engine.buttonDown(source("Mouse.Right"), ms(30));
        h.engine.buttonUp(source("Mouse.Right"), ms(40));
        CHECK(lastPoint(h.recorder).action != TouchAction::Up);

        h.engine.setCameraLock(Switch::On, ms(50));
        h.engine.buttonDown(source("Mouse.Right"), ms(60));
        CHECK(lastPoint(h.recorder).action == TouchAction::Up);
        h.engine.buttonUp(source("Mouse.Right"), ms(70));
    }
}

TEST_CASE("a manual skill from a profile without a cast key casts on a left click in cursor state (ADR-0077)")
{
    Harness h(skill("manual"));
    h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.5, 0.9}); });
    h.engine.buttonDown(source("KeyQ"), ms(0));
    h.engine.buttonUp(source("KeyQ"), ms(30));
    CHECK(h.engine.claims(source("Mouse.Left")));
    const std::size_t frames = h.recorder.frames.size();
    h.engine.buttonDown(source("Mouse.Left"), ms(40));
    h.engine.buttonUp(source("Mouse.Left"), ms(45));
    CHECK(h.recorder.frames.size() == frames);

    h.engine.cursor(CursorPhase::Down, {0.5, 0.9}, ms(60));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(h.engine.activeTouches() == 0);
    h.engine.cursor(CursorPhase::Up, {0.5, 0.9}, ms(80));
    CHECK_FALSE(h.engine.claims(source("Mouse.Left")));

    h.engine.cursor(CursorPhase::Down, {0.1, 0.1}, ms(100));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.cursor(CursorPhase::Up, {0.1, 0.1}, ms(120));
    CHECK(h.engine.activeTouches() == 0);
}

TEST_CASE("in cursor state a left click goes to a binding that claims the cursor instead of touching (ADR-0077)")
{
    Harness h(R"("controls":[{"id":"pick","type":"hold","inputs":{"press":{"input":"Mouse.Left","claim":"cursor"}}}])");
    CHECK(h.engine.claims(source("Mouse.Left")));
    h.engine.cursor(CursorPhase::Down, {0.1, 0.1}, ms(0));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(lastPoint(h.recorder).x > 20000);
    h.engine.cursor(CursorPhase::Up, {0.1, 0.1}, ms(20));
    CHECK(h.engine.activeTouches() == 0);

    const std::size_t frames = h.recorder.frames.size();
    h.engine.buttonDown(source("Mouse.Left"), ms(40));
    h.engine.buttonUp(source("Mouse.Left"), ms(50));
    CHECK(h.recorder.frames.size() == frames);
}

TEST_CASE("a held mouse binding the new lock state does not claim ends there and fires nothing new (ADR-0077)")
{
    SUBCASE("a held button")
    {
        Harness h(R"("controls":[{"id":"crouch","type":"hold","inputs":{"press":"Mouse.X1"}},
                                 {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
        h.engine.setCameraLock(Switch::On, ms(0));
        h.engine.buttonDown(source("Mouse.X1"), ms(10));
        CHECK(h.engine.activeTouches() == 1);
        h.engine.setCameraLock(Switch::Off, ms(20));
        CHECK(h.engine.activeTouches() == 0);
        const std::size_t frames = h.recorder.frames.size();
        h.engine.buttonUp(source("Mouse.X1"), ms(30));
        CHECK(h.recorder.frames.size() == frames);
    }
    SUBCASE("a running turbo")
    {
        Harness h(R"("controls":[{"id":"spray","type":"turbo","inputs":{"press":"Mouse.Right"},"rateHz":10},
                                 {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
        h.engine.setCameraLock(Switch::On, ms(0));
        h.engine.buttonDown(source("Mouse.Right"), ms(10));
        CHECK(h.engine.activeTouches() == 1);
        h.engine.setCameraLock(Switch::Off, ms(20));
        const std::size_t frames = h.recorder.frames.size();
        h.engine.tick(ms(500));
        CHECK(h.engine.activeTouches() == 0);
        for (std::size_t i = frames; i < h.recorder.frames.size(); ++i) {
            for (const auto& point : h.recorder.frames[i].points) {
                CHECK(point.action != TouchAction::Down);
            }
        }
        CHECK_FALSE(h.engine.nextWakeup().has_value());
        h.engine.buttonUp(source("Mouse.Right"), ms(600));
    }
}

TEST_CASE("a left-button binding claimed in both states is let go by the button, not by the window dropping its cursor (ADR-0077)")
{
    Harness h(R"("controls":[{"id":"fire","type":"hold","inputs":{"press":{"input":"Mouse.Left","claim":"both"}}},
                             {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
    h.engine.cursor(CursorPhase::Down, {0.1, 0.1}, ms(0));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.setCameraLock(Switch::On, ms(10));
    h.engine.cursor(CursorPhase::Cancel, {}, ms(15));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.buttonUp(source("Mouse.Left"), ms(30));
    CHECK(h.engine.activeTouches() == 0);
}

TEST_CASE("while a manual skill aims its cast key takes the click, other bindings on that key wait (ADR-0077)")
{
    Harness h(R"("controls":[{"id":"skill","type":"skill","inputs":{"press":"KeyQ","cancel":"Space"},"direction":"cursor","cast":"manual"},
        {"id":"pick","type":"hold","anchor":"topLeft","offset":[0.1,0.1],"inputs":{"press":{"input":"Mouse.Left","claim":"cursor"}}}])");
    h.engine.setCursorLocator([] { return std::optional<Vec2>(Vec2{0.5, 0.9}); });
    h.engine.buttonDown(source("KeyQ"), ms(0));
    h.engine.buttonUp(source("KeyQ"), ms(30));
    h.engine.cursor(CursorPhase::Down, {0.5, 0.9}, ms(60));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(h.engine.activeTouches() == 0);
    h.engine.cursor(CursorPhase::Up, {0.5, 0.9}, ms(80));
    CHECK(h.engine.activeTouches() == 0);

    h.engine.cursor(CursorPhase::Down, {0.1, 0.1}, ms(100));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.cursor(CursorPhase::Up, {0.1, 0.1}, ms(120));
    CHECK(h.engine.activeTouches() == 0);
}

TEST_CASE("the engine tells logic about the user's key presses, variables and switches, never about logic's own presses (ADR-0081, R11)")
{
    using Events = std::vector<std::pair<std::string, bool>>;
    Harness h(R"("controls":[{"id":"jump","type":"hold","offset":[0.3,0.2],"inputs":{"press":"Space"}},
                             {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],"radius":0.1,
                              "inputs":{"up":"KeyW","down":"KeyS"}},
                             {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}],
                 "vars":[{"id":"aiming"}])");
    h.engine.buttonDown(source("Space"), ms(0));
    h.engine.buttonUp(source("Space"), ms(40));
    CHECK(h.recorder.presses == Events{{"jump", true}, {"jump", false}});

    h.engine.buttonDown(source("KeyW"), ms(100));
    h.engine.buttonDown(source("KeyS"), ms(110));
    h.engine.buttonUp(source("KeyW"), ms(120));
    CHECK(h.recorder.presses.size() == 3);
    h.engine.buttonUp(source("KeyS"), ms(130));
    CHECK(h.recorder.presses.back() == std::pair<std::string, bool>{"move", false});
    CHECK(h.recorder.presses.size() == 4);

    const std::size_t frames = h.recorder.frames.size();
    CHECK(h.engine.pressControl("jump", true, ms(200)));
    CHECK(h.recorder.lastAction() == TouchAction::Down);
    CHECK(h.engine.pressControl("jump", false, ms(260)));
    CHECK(h.recorder.lastAction() == TouchAction::Up);
    CHECK(h.recorder.frames.size() == frames + 2);
    CHECK(h.engine.pressControl("move", true, ms(270)));
    CHECK(h.engine.pressControl("move", false, ms(280)));
    CHECK(h.recorder.frames.size() == frames + 2);
    CHECK_FALSE(h.engine.pressControl("nope", true, ms(290)));
    CHECK(h.recorder.presses.size() == 4);

    CHECK(h.engine.setState("aiming", Switch::Toggle, ms(300)));
    CHECK(h.engine.setState("aiming", Switch::On, ms(320)));
    CHECK(h.engine.setState("aiming", Switch::Off, ms(330)));
    CHECK(h.recorder.stateChanges == Events{{"aiming", true}, {"aiming", false}});

    h.engine.buttonDown(source("Space"), ms(400));
    CHECK(h.recorder.presses.back() == std::pair<std::string, bool>{"jump", true});
    CHECK(h.engine.setControlEnabled("jump", Switch::Toggle, ms(410)));
    CHECK(h.recorder.enabledChanges == Events{{"jump", false}});
    CHECK(h.engine.controlEnabled("jump") == false);
    CHECK(h.recorder.presses.back() == std::pair<std::string, bool>{"jump", false});
    h.engine.buttonUp(source("Space"), ms(420));
    h.engine.buttonDown(source("Space"), ms(440));
    CHECK(h.recorder.presses.size() == 6);
    h.engine.buttonUp(source("Space"), ms(450));
    CHECK(h.engine.setControlEnabled("jump", Switch::On, ms(460)));
    CHECK(h.recorder.enabledChanges == Events{{"jump", false}, {"jump", true}});
    CHECK(h.engine.controlEnabled("jump") == true);
    CHECK_FALSE(h.engine.controlEnabled("nope").has_value());

    h.engine.buttonDown(source("KeyW"), ms(500));
    h.engine.resetAll(ms(510));
    CHECK(h.recorder.presses.back() == std::pair<std::string, bool>{"move", false});
    CHECK(h.recorder.presses.size() == 8);
    h.engine.buttonUp(source("KeyW"), ms(520));
    CHECK(h.recorder.presses.size() == 8);
}

TEST_CASE("components report touching, stick deflection, walking and skill casts to the listener (ADR-0091)")
{
    class Statuses final : public TouchFrameSink, public EngineListener
    {
    public:
        std::vector<std::pair<std::string, ControlStatus>> seen;
        bool trySend(const TouchFrame&) override { return true; }
        void pointerState(PointerState) override {}
        void notice(EngineNotice) override {}
        void liveChanged(const std::vector<bool>&) override {}
        void statusChanged(std::string_view id, const ControlStatus& status) override { seen.emplace_back(id, status); }
    };
    Statuses statuses;
    MappingEngine engine(
        Harness::load(R"("controls":[{"id":"fire","type":"hold","inputs":{"press":"KeyF"}},
                         {"id":"move","type":"wasd","inputs":{"up":"KeyW"},"offset":[0.2,0.7]},
                         {"id":"walk","type":"rightWalk","inputs":{"stop":"KeyH"},"offset":[0.2,0.7]},
                         {"id":"skill","type":"skill","inputs":{"press":"KeyQ","cancel":"KeyC"},"offset":[0.8,0.7]}])"),
        EngineConfig{.screen = {.width = 2400, .height = 1080}, .refreshHz = 60.0, .pacingTick = Microseconds{0}},
        statuses, statuses);

    engine.buttonDown(source("KeyF"), ms(0));
    REQUIRE_FALSE(statuses.seen.empty());
    CHECK(statuses.seen.back().first == "fire");
    CHECK(statuses.seen.back().second.touching);
    CHECK(engine.controlStatus("fire")->touching);
    engine.buttonUp(source("KeyF"), ms(10));
    CHECK_FALSE(engine.controlStatus("fire")->touching);

    engine.buttonDown(source("KeyW"), ms(20));
    engine.tick(ms(40));
    const ControlStatus pushed = *engine.controlStatus("move");
    CHECK(pushed.touching);
    CHECK(pushed.stick.x == doctest::Approx(0.0));
    CHECK(pushed.stick.y == doctest::Approx(-1.0));
    engine.buttonUp(source("KeyW"), ms(60));
    CHECK(*engine.controlStatus("move") == ControlStatus{});

    CHECK(engine.walkControl("walk", {0.9, 0.5}, ms(100)));
    CHECK(engine.controlStatus("walk")->walking);
    CHECK(engine.pressSlot("walk", Slot::Stop, true, ms(110)));
    CHECK_FALSE(engine.controlStatus("walk")->walking);

    engine.buttonDown(source("KeyQ"), ms(200));
    engine.tick(ms(220));
    CHECK(engine.controlStatus("skill")->aiming);
    engine.buttonDown(source("KeyC"), ms(230));
    CHECK_FALSE(engine.controlStatus("skill")->aiming);
    CHECK(engine.controlStatus("skill")->cancelled);
    engine.buttonUp(source("KeyC"), ms(240));
    engine.buttonUp(source("KeyQ"), ms(250));
    engine.buttonDown(source("KeyQ"), ms(300));
    CHECK_FALSE(engine.controlStatus("skill")->cancelled);
    CHECK_FALSE(engine.controlStatus("missing").has_value());
}

TEST_CASE("a script retunes a component in place: a finger already down stays, the next press uses the new values "
          "(ADR-0091)")
{
    constexpr std::string_view body =
        R"("controls":[{"id":"aim","type":"hold","offset":[0.3,0.2],"inputs":{"press":"KeyA"}},
                       {"id":"burst","type":"turbo","offset":[0.6,0.2],"inputs":{"press":"KeyF"},"rateHz":10}])";
    Harness h(body);
    const Profile profile = Harness::load(body);
    h.engine.buttonDown(source("KeyA"), ms(0));
    const gs::protocol::TouchPoint before = lastPoint(h.recorder);
    Control moved = profile.controls[0];
    moved.offset = {0.5, 0.2};
    CHECK(h.engine.retuneControl(moved, ms(10)));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.buttonUp(source("KeyA"), ms(20));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(lastPoint(h.recorder).x == before.x);
    h.engine.buttonDown(source("KeyA"), ms(30));
    CHECK(lastPoint(h.recorder).action == TouchAction::Down);
    CHECK(lastPoint(h.recorder).x > before.x);
    h.engine.buttonUp(source("KeyA"), ms(40));

    h.engine.buttonDown(source("KeyF"), ms(1000));
    h.engine.tick(ms(1034));
    Control faster = profile.controls[1];
    std::get<TurboControl>(faster.kind).rateHz = 20;
    CHECK(h.engine.retuneControl(faster, ms(1040)));
    h.engine.tick(ms(1100));
    h.engine.tick(ms(1125));
    CHECK(h.engine.nextWakeup() == ms(1150));

    Control missing = profile.controls[0];
    missing.id = "missing";
    CHECK_FALSE(h.engine.retuneControl(missing, ms(1200)));
    Control other = profile.controls[0];
    other.kind = WasdControl{};
    CHECK_FALSE(h.engine.retuneControl(other, ms(1200)));
}

TEST_CASE("scripts push sticks, turn views, press any slot and reset components by id (ADR-0091)")
{
    Harness h(R"("controls":[{"id":"move","type":"wasd","inputs":{"up":"KeyW"},"offset":[0.2,0.7]},
                {"id":"view","type":"camera","inputs":{"look":"Mouse.Move"},"offset":[0.6,0.4]},
                {"id":"zoom","type":"pinch","inputs":{"zoomIn":"Equal"},"offset":[0.5,0.5]}])");
    CHECK(h.engine.pushControl("move", {1.0, 0.0}, ms(0)));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.tick(ms(20));
    CHECK(h.engine.controlStatus("move")->stick.x == doctest::Approx(1.0));
    CHECK(h.engine.resetControl("move", ms(30)));
    CHECK(h.engine.activeTouches() == 0);
    CHECK(*h.engine.controlStatus("move") == ControlStatus{});

    CHECK(h.engine.turnControl("view", {40.0, 0.0}, ms(40)));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(h.engine.controlStatus("view")->touching);
    CHECK(h.engine.resetControl("view", ms(50)));
    CHECK(h.engine.activeTouches() == 0);

    CHECK(h.engine.pressSlot("zoom", Slot::ZoomOut, true, ms(60)));
    CHECK(h.engine.activeTouches() == 2);
    CHECK(slotNamed("zoomOut") == Slot::ZoomOut);
    CHECK(slotNamed("cast") == Slot::Cast);
    CHECK_FALSE(slotNamed("jump").has_value());
    CHECK_FALSE(h.engine.pushControl("missing", {1.0, 0.0}, ms(70)));
    CHECK_FALSE(h.engine.resetControl("missing", ms(70)));

    CHECK(h.engine.setControlEnabled("move", Switch::Off, ms(80)));
    CHECK(h.engine.pushControl("move", {1.0, 0.0}, ms(90)));
    CHECK(h.engine.controlStatus("move")->touching == false);
}

TEST_CASE("a direction wheel presses its middle, slides to the cell the mouse points to and lifts there (ADR-0094)")
{
    Harness h(R"("controls":[{"id":"look","type":"camera","anchor":"right","offset":[-0.5,0],"inputs":{"look":"Mouse.Move"}},
                             {"id":"wheel","type":"dial","radius":0.1,"cells":4,"inputs":{"press":"KeyQ","look":"Mouse.Move"}}])");
    h.engine.setCameraLock(Switch::On, ms(0));
    h.engine.buttonDown(source("KeyQ"), ms(0));
    REQUIRE(h.recorder.frames.size() == 1);
    CHECK(lastPoint(h.recorder).action == TouchAction::Down);
    CHECK(lastPoint(h.recorder).x == 32768);
    CHECK(lastPoint(h.recorder).y == 32768);
    const std::uint8_t finger = lastPoint(h.recorder).pointerId;

    h.engine.motion(source("Mouse.Move"), 5, 0, ms(10));
    CHECK(h.recorder.frames.size() == 1);
    CHECK(h.engine.controlStatus("wheel")->cell == -1);
    h.engine.motion(source("Mouse.Move"), 20, 0, ms(20));
    CHECK(lastPoint(h.recorder).pointerId == finger);
    CHECK(lastPoint(h.recorder).action == TouchAction::Move);
    CHECK(lastPoint(h.recorder).x > 35000);
    CHECK(lastPoint(h.recorder).y == 32768);
    CHECK(h.engine.controlStatus("wheel")->cell == 1);

    h.engine.motion(source("Mouse.Move"), 0, -100, ms(30));
    CHECK(lastPoint(h.recorder).x == 32768);
    CHECK(lastPoint(h.recorder).y == 26214);
    CHECK(h.engine.controlStatus("wheel")->cell == 0);
    CHECK(h.engine.activeTouches() == 1);

    h.engine.buttonUp(source("KeyQ"), ms(40));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(lastPoint(h.recorder).y == 26214);
    CHECK(h.engine.controlStatus("wheel")->cell == -1);
    CHECK(h.engine.activeTouches() == 0);

    h.engine.buttonDown(source("KeyQ"), ms(50));
    h.engine.motion(source("Mouse.Move"), 30, 0, ms(60));
    CHECK(h.engine.controlStatus("wheel")->cell == 1);
    h.engine.motion(source("Mouse.Move"), -30, 0, ms(70));
    CHECK(lastPoint(h.recorder).x == 32768);
    CHECK(h.engine.controlStatus("wheel")->cell == -1);
    h.engine.buttonUp(source("KeyQ"), ms(80));
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(lastPoint(h.recorder).x == 32768);
    CHECK(lastPoint(h.recorder).y == 32768);

    h.engine.motion(source("Mouse.Move"), 100, 0, ms(90));
    CHECK(h.engine.activeTouches() == 1);
    CHECK(h.engine.controlStatus("look")->touching);
}

TEST_CASE("an action wheel opens without touching, points by mouse and runs the pointed cell on release (ADR-0094)")
{
    Harness h(R"("controls":[{"id":"potion","type":"tap","anchor":"topLeft","offset":[0.2,0.2],"inputs":{"press":"KeyH"}},
                             {"id":"items","type":"actionWheel","inputs":{"press":"KeyB","look":"Mouse.Move"},
                              "cells":[{"name":"potion","action":"control","control":"potion"},
                                       {"name":"spot","action":"tap","at":[0.1,0]},
                                       {"name":"logic","action":"logic"},
                                       {}]}])");
    h.engine.buttonDown(source("KeyB"), ms(0));
    CHECK(h.recorder.frames.empty());
    CHECK(h.engine.controlStatus("items")->touching);
    CHECK(h.engine.controlStatus("items")->cell == -1);
    REQUIRE_FALSE(h.recorder.wheels.empty());
    CHECK(h.recorder.wheels.back() == std::tuple<std::string, bool, int>{"items", true, -1});

    h.engine.motion(source("Mouse.Move"), 0, -30, ms(10));
    CHECK(h.engine.controlStatus("items")->cell == 0);
    CHECK(h.recorder.wheels.back() == std::tuple<std::string, bool, int>{"items", true, 0});
    h.engine.buttonUp(source("KeyB"), ms(20));
    CHECK_FALSE(h.engine.controlStatus("items")->touching);
    CHECK(h.recorder.wheels.back() == std::tuple<std::string, bool, int>{"items", false, -1});
    REQUIRE(h.recorder.frames.size() == 1);
    CHECK(lastPoint(h.recorder).action == TouchAction::Down);
    CHECK(lastPoint(h.recorder).x < 32768);
    const std::optional<Microseconds> release = h.engine.nextWakeup();
    REQUIRE(release.has_value());
    h.engine.tick(*release);
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);
    CHECK(h.engine.activeTouches() == 0);

    const std::size_t before = h.recorder.frames.size();
    h.engine.buttonDown(source("KeyB"), ms(1000));
    h.engine.motion(source("Mouse.Move"), 30, 0, ms(1010));
    CHECK(h.engine.controlStatus("items")->cell == 1);
    h.engine.buttonUp(source("KeyB"), ms(1020));
    REQUIRE(h.recorder.frames.size() == before + 1);
    CHECK(lastPoint(h.recorder).action == TouchAction::Down);
    CHECK(lastPoint(h.recorder).x > 32768);
    h.engine.tick(*h.engine.nextWakeup());
    CHECK(lastPoint(h.recorder).action == TouchAction::Up);

    const std::size_t afterTap = h.recorder.frames.size();
    h.engine.buttonDown(source("KeyB"), ms(2000));
    h.engine.motion(source("Mouse.Move"), 0, 30, ms(2010));
    CHECK(h.engine.controlStatus("items")->cell == 2);
    h.engine.buttonUp(source("KeyB"), ms(2020));
    h.engine.buttonDown(source("KeyB"), ms(3000));
    h.engine.buttonUp(source("KeyB"), ms(3010));
    h.engine.buttonDown(source("KeyB"), ms(4000));
    h.engine.motion(source("Mouse.Move"), -30, 0, ms(4010));
    CHECK(h.engine.controlStatus("items")->cell == 3);
    h.engine.buttonUp(source("KeyB"), ms(4020));
    CHECK(h.recorder.frames.size() == afterTap);
    CHECK_FALSE(h.engine.nextWakeup().has_value());

    h.engine.buttonDown(source("KeyB"), ms(5000));
    h.engine.motion(source("Mouse.Move"), 0, -30, ms(5010));
    h.engine.resetAll(ms(5020));
    CHECK_FALSE(h.engine.controlStatus("items")->touching);
    h.engine.buttonUp(source("KeyB"), ms(5030));
    CHECK(h.recorder.frames.size() == afterTap);
}

TEST_CASE("a multi-press holds every place in one frame while its key is held and lifts them together (ADR-0094)")
{
    Harness h(R"("controls":[{"id":"peekFire","type":"multiPress","points":[[0.1,0],[0,0.2]],"inputs":{"press":"KeyE"}}])");
    h.engine.buttonDown(source("KeyE"), ms(0));
    REQUIRE(h.recorder.frames.size() == 1);
    REQUIRE(h.recorder.frames[0].points.size() == 3);
    for (const auto& point : h.recorder.frames[0].points) {
        CHECK(point.action == TouchAction::Down);
    }
    CHECK(h.recorder.frames[0].points[0].x == 32768);
    CHECK(h.recorder.frames[0].points[1].x > 32768);
    CHECK(h.recorder.frames[0].points[2].y > 32768);
    CHECK(h.engine.activeTouches() == 3);
    CHECK(h.engine.controlStatus("peekFire")->touching);
    CHECK_FALSE(h.engine.nextWakeup().has_value());

    h.engine.buttonUp(source("KeyE"), ms(500));
    REQUIRE(h.recorder.frames.size() == 2);
    REQUIRE(h.recorder.frames[1].points.size() == 3);
    for (const auto& point : h.recorder.frames[1].points) {
        CHECK(point.action == TouchAction::Up);
    }
    CHECK(h.engine.activeTouches() == 0);
    CHECK_FALSE(h.engine.controlStatus("peekFire")->touching);
}

TEST_CASE("a temporary cursor frees the mouse while held, keeps the lock and only works while locked (ADR-0094)")
{
    Harness h(R"("controls":[{"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}},
                             {"id":"look","type":"camera","lockKey":"Backquote","inputs":{"look":"Mouse.Move"}},
                             {"id":"pointer","type":"tempCursor","inputs":{"press":"AltLeft"}}])");
    h.engine.buttonDown(source("AltLeft"), ms(0));
    CHECK_FALSE(h.engine.peeking());
    CHECK(h.recorder.peeks.empty());
    h.engine.buttonUp(source("AltLeft"), ms(5));

    h.engine.setCameraLock(Switch::On, ms(10));
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(10));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.buttonDown(source("AltLeft"), ms(20));
    CHECK(h.engine.peeking());
    CHECK(h.engine.cameraLocked());
    CHECK(h.recorder.peeks == std::vector<bool>{true});
    CHECK(h.engine.activeTouches() == 0);
    CHECK(h.engine.controlStatus("pointer")->touching);

    h.engine.buttonDown(source("Mouse.Left"), ms(30));
    CHECK(h.engine.activeTouches() == 0);
    h.engine.cursor(CursorPhase::Down, {0.3, 0.4}, ms(30));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.cursor(CursorPhase::Up, {0.3, 0.4}, ms(40));
    h.engine.buttonUp(source("Mouse.Left"), ms(40));
    CHECK(h.engine.activeTouches() == 0);
    h.engine.motion(source("Mouse.Move"), 100, 0, ms(50));
    CHECK(h.engine.activeTouches() == 0);

    h.engine.buttonUp(source("AltLeft"), ms(60));
    CHECK_FALSE(h.engine.peeking());
    CHECK(h.recorder.peeks == std::vector<bool>{true, false});
    CHECK(h.recorder.pointerStates == std::vector<PointerState>{PointerState::Captured});
    CHECK_FALSE(h.engine.controlStatus("pointer")->touching);
    h.engine.buttonDown(source("Mouse.Left"), ms(70));
    CHECK(h.engine.activeTouches() == 1);
    h.engine.buttonUp(source("Mouse.Left"), ms(80));

    h.engine.buttonDown(source("AltLeft"), ms(90));
    CHECK(h.engine.peeking());
    h.engine.resetAll(ms(100));
    CHECK_FALSE(h.engine.peeking());
    CHECK(h.engine.cameraLocked());
    CHECK(h.recorder.peeks == std::vector<bool>{true, false, true, false});
}
