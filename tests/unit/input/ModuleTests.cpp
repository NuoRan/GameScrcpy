#include "gs/input/MappingEngine.h"
#include "gs/input/Modules.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
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
using gs::protocol::TouchPoint;

namespace {

constexpr Microseconds ms(long long value)
{
    return Microseconds{value * 1000};
}

constexpr Microseconds kFrameTick{1'000'000 / 120};

InputSource source(std::string_view name)
{
    const auto parsed = parseSource(name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

Chord chord(std::string_view text)
{
    const auto parsed = parseChord(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

using InputScript = std::function<ModuleStatus(const ModuleEvent&, ModuleOutput&)>;
using OutputScript = std::function<ModuleStatus(const FingerCommand&, ModuleOutput&)>;
using TimeScript = std::function<ModuleStatus(Microseconds, ModuleOutput&)>;

struct AttachmentScript {
    InputScript input;
    OutputScript output;
    TimeScript wake;
};

struct BehaviorScript {
    BehaviorShape shape;
    InputScript input;
    TimeScript frame;
    TimeScript wake;
    int* resets = nullptr;
};

class ScriptedAttachment final : public AttachmentInstance
{
public:
    explicit ScriptedAttachment(AttachmentScript script)
        : m_script(std::move(script))
    {
    }

    ModuleStatus input(const ModuleEvent& event, ModuleOutput& out) override
    {
        return m_script.input ? m_script.input(event, out) : ModuleStatus::Ok;
    }

    ModuleStatus output(const FingerCommand& command, ModuleOutput& out) override
    {
        return m_script.output ? m_script.output(command, out) : ModuleStatus::Ok;
    }

    ModuleStatus wake(Microseconds now, ModuleOutput& out) override
    {
        return m_script.wake ? m_script.wake(now, out) : ModuleStatus::Ok;
    }

    void reset() override {}

private:
    AttachmentScript m_script;
};

class ScriptedBehavior final : public BehaviorInstance
{
public:
    explicit ScriptedBehavior(BehaviorScript script)
        : m_script(std::move(script))
    {
    }

    ModuleStatus input(const ModuleEvent& event, ModuleOutput& out) override
    {
        return m_script.input ? m_script.input(event, out) : ModuleStatus::Ok;
    }

    ModuleStatus frame(Microseconds now, ModuleOutput& out) override
    {
        return m_script.frame ? m_script.frame(now, out) : ModuleStatus::Ok;
    }

    ModuleStatus wake(Microseconds now, ModuleOutput& out) override
    {
        return m_script.wake ? m_script.wake(now, out) : ModuleStatus::Ok;
    }

    void reset() override
    {
        if (m_script.resets != nullptr) {
            ++*m_script.resets;
        }
    }

private:
    BehaviorScript m_script;
};

class Scatter final : public AttachmentInstance
{
public:
    explicit Scatter(std::uint64_t seed)
        : m_state(seed)
    {
    }

    ModuleStatus input(const ModuleEvent&, ModuleOutput&) override { return ModuleStatus::Ok; }

    ModuleStatus output(const FingerCommand& command, ModuleOutput& out) override
    {
        if (command.action == FingerAction::Down) {
            m_offset = {next() * 0.02 - 0.01, next() * 0.02 - 0.01};
        }
        out.commands[0].position = {command.position.x + m_offset.x, command.position.y + m_offset.y};
        return ModuleStatus::Ok;
    }

    ModuleStatus wake(Microseconds, ModuleOutput&) override { return ModuleStatus::Ok; }
    void reset() override {}

private:
    double next()
    {
        m_state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = m_state;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return static_cast<double>((z ^ (z >> 31U)) >> 11U) / 9007199254740992.0;
    }

    std::uint64_t m_state;
    Vec2 m_offset;
};

class Glide final : public AttachmentInstance
{
public:
    Glide(int& resets, int& skippedWakes, int& skippedMoves)
        : m_resets(resets)
        , m_skippedWakes(skippedWakes)
        , m_skippedMoves(skippedMoves)
    {
    }

    ModuleStatus input(const ModuleEvent&, ModuleOutput&) override { return ModuleStatus::Ok; }

    ModuleStatus output(const FingerCommand& command, ModuleOutput& out) override
    {
        if (command.action != FingerAction::Move) {
            m_down = command.action == FingerAction::Down;
            return ModuleStatus::Ok;
        }
        if (!m_down) {
            return ModuleStatus::Ok;
        }
        if (m_skippedMoves > 0) {
            --m_skippedMoves;
            return ModuleStatus::Skipped;
        }
        m_finger = command.finger;
        m_target = command.position;
        out.commands.clear();
        out.wakeAt = command.at + ms(8);
        return ModuleStatus::Ok;
    }

    ModuleStatus wake(Microseconds now, ModuleOutput& out) override
    {
        if (m_skippedWakes > 0) {
            --m_skippedWakes;
            return ModuleStatus::Skipped;
        }
        out.commands.push_back({.action = FingerAction::Move, .finger = m_finger, .position = m_target, .at = now});
        return ModuleStatus::Ok;
    }

    void reset() override
    {
        ++m_resets;
        m_down = false;
    }

private:
    int& m_resets;
    int& m_skippedWakes;
    int& m_skippedMoves;
    bool m_down = false;
    std::uint32_t m_finger = 0;
    Vec2 m_target;
};

using Trouble = std::tuple<ModuleTroubleKind, std::string, std::string>;

class FakeFactory final : public ModuleFactory
{
public:
    std::map<std::string, AttachmentScript, std::less<>> attachments;
    std::map<std::string, std::function<std::unique_ptr<AttachmentInstance>(const ModuleRequest&)>, std::less<>>
        makers;
    std::map<std::string, BehaviorScript, std::less<>> behaviors;
    std::vector<std::string> made;
    std::vector<std::uint64_t> seeds;
    std::vector<std::string> params;
    std::vector<Trouble> troubles;
    std::vector<GlobalAttachment> globalList;

    const std::vector<GlobalAttachment>& globals() const noexcept override { return globalList; }

    std::unique_ptr<AttachmentInstance> attachment(const ModuleRequest& request) override
    {
        if (const auto maker = makers.find(request.module); maker != makers.end()) {
            note(request);
            return maker->second(request);
        }
        const auto found = attachments.find(request.module);
        if (found == attachments.end()) {
            return nullptr;
        }
        note(request);
        return std::make_unique<ScriptedAttachment>(found->second);
    }

    std::unique_ptr<BehaviorInstance> behavior(const ModuleRequest& request, BehaviorShape& shape) override
    {
        const auto found = behaviors.find(request.module);
        if (found == behaviors.end()) {
            return nullptr;
        }
        note(request);
        shape = found->second.shape;
        return std::make_unique<ScriptedBehavior>(found->second);
    }

    void trouble(const ModuleTrouble& trouble) override
    {
        troubles.emplace_back(trouble.kind, std::string(trouble.module), std::string(trouble.control));
    }

private:
    void note(const ModuleRequest& request)
    {
        made.emplace_back(request.module);
        seeds.push_back(request.seed);
        params.emplace_back(request.params);
    }
};

class Recorder final : public TouchFrameSink, public EngineListener
{
public:
    std::vector<TouchFrame> frames;

    bool trySend(const TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }
    void pointerState(PointerState) override {}
    void notice(EngineNotice) override {}
    void liveChanged(const std::vector<bool>&) override {}

    [[nodiscard]] std::vector<TouchPoint> points() const
    {
        std::vector<TouchPoint> all;
        for (const TouchFrame& frame : frames) {
            all.insert(all.end(), frame.points.begin(), frame.points.end());
        }
        return all;
    }

    [[nodiscard]] TouchPoint last() const
    {
        REQUIRE_FALSE(frames.empty());
        REQUIRE_FALSE(frames.back().points.empty());
        return frames.back().points.back();
    }
};

Profile loadControls(std::string_view controls)
{
    const LoadResult result =
        loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":)" + std::string(controls) + "}");
    REQUIRE(result.ok());
    return result.profile;
}

EngineConfig configWith(std::shared_ptr<ModuleFactory> modules, std::uint32_t seed = 1)
{
    return EngineConfig{.screen = {.width = 2400, .height = 1080},
                        .refreshHz = 60.0,
                        .pacingTick = Microseconds{0},
                        .seed = seed,
                        .modules = std::move(modules)};
}

struct Rig {
    Recorder recorder;
    std::shared_ptr<FakeFactory> factory;
    MappingEngine engine;

    Rig(std::string_view controls, std::shared_ptr<FakeFactory> modules, std::uint32_t seed = 1)
        : factory(std::move(modules))
        , engine(loadControls(controls), configWith(factory, seed), recorder, recorder)
    {
    }
};

AttachmentScript shiftBy(double dx)
{
    return {.output = [dx](const FingerCommand&, ModuleOutput& out) {
        out.commands[0].position.x += dx;
        return ModuleStatus::Ok;
    }};
}

AttachmentScript scaleBy(double factor)
{
    return {.output = [factor](const FingerCommand&, ModuleOutput& out) {
        out.commands[0].position.x *= factor;
        return ModuleStatus::Ok;
    }};
}

bool has(const std::vector<Diagnostic>& found, Severity severity, DiagnosticCode code, std::string_view path,
         std::string_view detail = {})
{
    return std::ranges::any_of(found, [&](const Diagnostic& d) {
        return d.severity == severity && d.code == code && d.path == path && (detail.empty() || d.detail == detail);
    });
}

}

TEST_CASE("attachments see a component's input one after the other and may change or swallow it (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    std::vector<std::string> seen;
    factory->attachments["test.mods/turn"] = {.input = [&seen](const ModuleEvent& event, ModuleOutput& out) {
        seen.push_back("turn " + std::to_string(event.slot));
        if (event.slot == 0) {
            out.events[0].slot = 3;
        }
        return ModuleStatus::Ok;
    }};
    factory->attachments["test.mods/watch"] = {.input = [&seen](const ModuleEvent& event, ModuleOutput&) {
        seen.push_back("watch " + std::to_string(event.slot));
        return ModuleStatus::Ok;
    }};
    factory->attachments["test.mods/swallow"] = {.input = [](const ModuleEvent& event, ModuleOutput& out) {
        if (event.kind == ModuleInput::KeyDown) {
            out.events.clear();
        }
        return ModuleStatus::Ok;
    }};
    Rig rig(R"([{"id":"move","type":"wasd","inputs":{"up":"KeyW","right":"KeyD"},
                 "attachments":[{"module":"test.mods/turn"},{"module":"test.mods/watch"}]},
                {"id":"fire","type":"hold","offset":[0.4,0],"inputs":{"press":"KeyF"},
                 "attachments":[{"module":"test.mods/swallow"}]}])",
            factory);
    CHECK(factory->made == std::vector<std::string>{"test.mods/turn", "test.mods/watch", "test.mods/swallow"});

    rig.engine.buttonDown(source("KeyW"), ms(0));
    CHECK(seen == std::vector<std::string>{"turn 0", "watch 3"});
    const TouchPoint pushed = rig.recorder.last();
    CHECK(pushed.action == TouchAction::Move);
    CHECK(pushed.x == fixedCoordinate(0.5 + 0.12 * 1080.0 / 2400.0));
    CHECK(pushed.y == fixedCoordinate(0.5));

    const std::size_t before = rig.recorder.frames.size();
    rig.engine.buttonDown(source("KeyF"), ms(10));
    rig.engine.buttonUp(source("KeyF"), ms(20));
    CHECK(rig.recorder.frames.size() == before);
    CHECK(rig.engine.activeTouches() == 1);

    rig.engine.buttonUp(source("KeyW"), ms(30));
    CHECK(seen == std::vector<std::string>{"turn 0", "watch 3", "turn 0", "watch 3"});
    CHECK(rig.recorder.last().action == TouchAction::Up);
    CHECK(rig.engine.activeTouches() == 0);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("a global add-on reaches every fitting component before its own, unless the component skips it or has its "
          "own copy (ADR-0073 revision 2026-11-08)")
{
    auto factory = std::make_shared<FakeFactory>();
    factory->attachments["test.mods/shift"] = shiftBy(0.1);
    factory->attachments["test.mods/halve"] = scaleBy(0.5);
    factory->globalList = {{.module = "test.mods/shift", .params = R"({"reach":3})", .appliesTo = {"hold", "tap"}}};
    Rig rig(R"([{"id":"fire","type":"hold","inputs":{"press":"KeyF"},
                 "attachments":[{"module":"test.mods/halve"}]},
                {"id":"jump","type":"tap","inputs":{"press":"Space"}},
                {"id":"walk","type":"wasd","inputs":{"up":"KeyW","down":"KeyS","left":"KeyA","right":"KeyD"}},
                {"id":"crouch","type":"hold","inputs":{"press":"KeyC"},
                 "skipGlobal":[{"module":"test.mods/shift"}]},
                {"id":"aim","type":"hold","inputs":{"press":"KeyR"},
                 "attachments":[{"module":"test.mods/shift","enabled":false}]}])",
            factory);

    rig.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(rig.recorder.last().x == fixedCoordinate((0.5 + 0.1) * 0.5));
    rig.engine.buttonUp(source("KeyF"), ms(10));

    rig.engine.buttonDown(source("KeyC"), ms(20));
    CHECK(rig.recorder.last().x == fixedCoordinate(0.5));
    rig.engine.buttonUp(source("KeyC"), ms(30));

    rig.engine.buttonDown(source("KeyR"), ms(40));
    CHECK(rig.recorder.last().x == fixedCoordinate(0.5));
    rig.engine.buttonUp(source("KeyR"), ms(50));

    const auto globalMade = std::ranges::count(rig.factory->made, std::string("test.mods/shift"));
    CHECK(globalMade == 2);
    REQUIRE(rig.factory->made.size() == 3);
    CHECK(rig.factory->made[0] == "test.mods/shift");
    CHECK(rig.factory->made[1] == "test.mods/halve");
    CHECK(rig.factory->params[0] == R"({"reach":3})");
    CHECK(rig.factory->troubles.empty());
}

TEST_CASE("attachments change the component's fingers in list order, insert points and move them later (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    factory->attachments["test.mods/shift"] = shiftBy(0.1);
    factory->attachments["test.mods/halve"] = scaleBy(0.5);
    factory->attachments["test.mods/trail"] = {.output = [](const FingerCommand& command, ModuleOutput& out) {
        if (command.action == FingerAction::Down) {
            out.commands.push_back({.action = FingerAction::Move,
                                    .finger = command.finger,
                                    .position = {command.position.x + 0.05, command.position.y},
                                    .at = command.at + ms(10)});
        } else if (command.action == FingerAction::Up) {
            out.commands[0].at = command.at + ms(50);
        }
        return ModuleStatus::Ok;
    }};
    Rig rig(R"([{"id":"fire","type":"hold","inputs":{"press":"KeyF"},
                 "attachments":[{"module":"test.mods/shift"},{"module":"test.mods/halve"}]},
                {"id":"slide","type":"hold","inputs":{"press":"KeyG"},
                 "attachments":[{"module":"test.mods/trail"}]}])",
            factory);

    rig.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(rig.recorder.last().action == TouchAction::Down);
    CHECK(rig.recorder.last().x == fixedCoordinate((0.5 + 0.1) * 0.5));
    rig.engine.buttonUp(source("KeyF"), ms(5));
    CHECK(rig.recorder.last().action == TouchAction::Up);
    CHECK(rig.recorder.last().x == fixedCoordinate((0.5 + 0.1) * 0.5));
    CHECK_FALSE(rig.engine.nextWakeup().has_value());

    rig.engine.buttonDown(source("KeyG"), ms(100));
    CHECK(rig.recorder.last().action == TouchAction::Down);
    CHECK(rig.recorder.last().x == fixedCoordinate(0.5));
    CHECK(rig.engine.nextWakeup() == ms(110));
    rig.engine.tick(ms(110));
    CHECK(rig.recorder.last().action == TouchAction::Move);
    CHECK(rig.recorder.last().x == fixedCoordinate(0.55));

    rig.engine.buttonUp(source("KeyG"), ms(200));
    CHECK(rig.recorder.last().action == TouchAction::Move);
    CHECK(rig.engine.activeTouches() == 1);
    CHECK(rig.engine.nextWakeup() == ms(250));
    rig.engine.tick(ms(249));
    CHECK(rig.engine.activeTouches() == 1);
    rig.engine.tick(ms(250));
    CHECK(rig.recorder.last().action == TouchAction::Up);
    CHECK(rig.engine.activeTouches() == 0);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("an attachment delays a key by swallowing it and giving it back when its wake time comes (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    factory->attachments["test.mods/late"] = {
        .input =
            [](const ModuleEvent& event, ModuleOutput& out) {
                if (event.kind == ModuleInput::KeyDown) {
                    out.events.clear();
                    out.wakeAt = event.at + ms(30);
                }
                return ModuleStatus::Ok;
            },
        .wake =
            [](Microseconds now, ModuleOutput& out) {
                out.events.push_back({.kind = ModuleInput::KeyDown, .slot = 0, .x = 0.0, .y = 0.0, .at = now});
                return ModuleStatus::Ok;
            },
    };
    Rig rig(R"([{"id":"fire","type":"hold","inputs":{"press":"KeyF"},"attachments":[{"module":"test.mods/late"}]}])",
            factory);
    rig.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(rig.recorder.frames.empty());
    CHECK(rig.engine.nextWakeup() == ms(30));
    rig.engine.tick(ms(30));
    CHECK(rig.recorder.last().action == TouchAction::Down);
    rig.engine.buttonUp(source("KeyF"), ms(40));
    CHECK(rig.recorder.last().action == TouchAction::Up);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("an add-on whose wake goes unanswered lets go of it and the stick finger goes where the stick wants it "
          "(issue 26)")
{
    int resets = 0;
    int skippedWakes = 0;
    int skippedMoves = 0;
    auto factory = std::make_shared<FakeFactory>();
    factory->makers["test.mods/glide"] = [&](const ModuleRequest&) -> std::unique_ptr<AttachmentInstance> {
        return std::make_unique<Glide>(resets, skippedWakes, skippedMoves);
    };
    Rig rig(R"([{"id":"move","type":"wasd","inputs":{"up":"KeyW","left":"KeyA"},
                 "attachments":[{"module":"test.mods/glide"}]}])",
            factory);
    Rig plain(R"([{"id":"move","type":"wasd","inputs":{"up":"KeyW","left":"KeyA"}}])", std::make_shared<FakeFactory>());

    rig.engine.buttonDown(source("KeyW"), ms(0));
    rig.engine.tick(ms(8));
    rig.engine.buttonDown(source("KeyA"), ms(20));
    rig.engine.tick(ms(28));
    const TouchPoint slanted = rig.recorder.last();
    rig.engine.buttonUp(source("KeyA"), ms(40));
    skippedWakes = 1;
    rig.engine.tick(ms(48));

    plain.engine.buttonDown(source("KeyW"), ms(0));
    plain.engine.buttonDown(source("KeyA"), ms(20));
    plain.engine.buttonUp(source("KeyA"), ms(40));
    const TouchPoint wanted = plain.recorder.last();

    CHECK(slanted.x != wanted.x);
    CHECK(rig.recorder.last().x == wanted.x);
    CHECK(rig.recorder.last().y == wanted.y);
    CHECK(resets == 1);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("an add-on whose move goes unanswered forgets the wake it asked for, so an old target never comes back "
          "(issue 26)")
{
    int resets = 0;
    int skippedWakes = 0;
    int skippedMoves = 0;
    auto factory = std::make_shared<FakeFactory>();
    factory->makers["test.mods/glide"] = [&](const ModuleRequest&) -> std::unique_ptr<AttachmentInstance> {
        return std::make_unique<Glide>(resets, skippedWakes, skippedMoves);
    };
    Rig rig(R"([{"id":"move","type":"wasd","inputs":{"up":"KeyW","left":"KeyA"},
                 "attachments":[{"module":"test.mods/glide"}]}])",
            factory);
    Rig plain(R"([{"id":"move","type":"wasd","inputs":{"up":"KeyW","left":"KeyA"}}])", std::make_shared<FakeFactory>());

    rig.engine.buttonDown(source("KeyW"), ms(0));
    rig.engine.tick(ms(8));
    rig.engine.buttonDown(source("KeyA"), ms(20));
    skippedMoves = 1;
    rig.engine.buttonUp(source("KeyA"), ms(22));
    rig.engine.tick(ms(28));
    rig.engine.tick(ms(60));

    plain.engine.buttonDown(source("KeyW"), ms(0));
    plain.engine.buttonDown(source("KeyA"), ms(20));
    plain.engine.buttonUp(source("KeyA"), ms(22));
    const TouchPoint wanted = plain.recorder.last();

    CHECK(rig.recorder.last().x == wanted.x);
    CHECK(rig.recorder.last().y == wanted.y);
    CHECK(resets == 1);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("a wheel notch reaches an attachment as one wheel event and the component as a press (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    std::vector<ModuleEvent> seen;
    factory->attachments["test.mods/watch"] = {.input = [&seen](const ModuleEvent& event, ModuleOutput&) {
        seen.push_back(event);
        return ModuleStatus::Ok;
    }};
    Rig rig(R"([{"id":"zoom","type":"hold","inputs":{"press":{"input":"Mouse.WheelUp","claim":"both"}},
                 "attachments":[{"module":"test.mods/watch"}]}])",
            factory);
    rig.engine.buttonDown(source("Mouse.WheelUp"), ms(0));
    rig.engine.buttonUp(source("Mouse.WheelUp"), ms(0));
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].kind == ModuleInput::Wheel);
    CHECK(seen[0].y == -1.0);
    CHECK(seen[0].x == 0.0);
    const std::vector<TouchPoint> points = rig.recorder.points();
    REQUIRE(points.size() == 2);
    CHECK(points[0].action == TouchAction::Down);
    CHECK(points[1].action == TouchAction::Up);
}

TEST_CASE("a control runs at most eight attachments and a profile with more does not load (ADR-0105)")
{
    std::string many = R"([{"id":"fire","type":"hold","inputs":{"press":"KeyF"},"attachments":[)";
    for (int index = 0; index < 9; ++index) {
        many += (index == 0 ? "" : ",") + std::string(R"({"module":"test.mods/a)") + std::to_string(index) + R"("})";
    }
    many += "]}]";
    const LoadResult refused =
        loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":)" + many + "}");
    CHECK_FALSE(refused.ok());
    CHECK(has(refused.diagnostics, Severity::Error, DiagnosticCode::OutOfRange, "$.controls[0].attachments", "9"));

    auto factory = std::make_shared<FakeFactory>();
    factory->attachments["test.mods/count"] = {};
    Profile profile;
    Control control;
    control.id = "fire";
    control.kind = HoldControl{.press = Binding{.input = chord("KeyF")}};
    control.attachments.assign(9, Attachment{.module = "test.mods/count", .enabled = true, .params = {}});
    control.attachments[1].enabled = false;
    profile.controls.push_back(control);
    Recorder recorder;
    MappingEngine engine(profile, configWith(factory), recorder, recorder);
    CHECK(factory->made.size() == 7);
}

TEST_CASE("a component's list of skipped global add-ons is kept on save and must name modules (ADR-0073 revision "
          "2026-11-08)")
{
    const std::string_view kept = R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"fire","type":"hold","inputs":{"press":"KeyF"},
         "skipGlobal":[{"module":"community.feel-samples/randomLanding"}]}]})";
    const LoadResult loaded = loadProfile(kept);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.profile.controls[0].skipGlobal.size() == 1);
    CHECK(loaded.profile.controls[0].skipGlobal[0].module == "community.feel-samples/randomLanding");
    const LoadResult again = loadProfile(saveProfile(loaded.profile));
    REQUIRE(again.ok());
    CHECK(again.profile.controls[0].skipGlobal == loaded.profile.controls[0].skipGlobal);
    const LoadResult plain = loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"fire","type":"hold","inputs":{"press":"KeyF"}}]})");
    REQUIRE(plain.ok());
    CHECK(saveProfile(plain.profile).find("skipGlobal") == std::string::npos);

    const LoadResult refused = loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"fire","type":"hold","inputs":{"press":"KeyF"},"skipGlobal":[{"module":"no slash"}]}]})");
    CHECK_FALSE(refused.ok());
    CHECK(has(refused.diagnostics, Severity::Error, DiagnosticCode::InvalidValue, "$.controls[0].skipGlobal[0].module",
              "no slash"));
}

TEST_CASE("the same seed gives the same fingers twice and each attachment gets its own seed (ADR-0105)")
{
    const auto run = [](std::uint32_t seed) {
        auto factory = std::make_shared<FakeFactory>();
        factory->makers["test.mods/scatter"] = [](const ModuleRequest& request) {
            return std::make_unique<Scatter>(request.seed);
        };
        Rig rig(R"([{"id":"fire","type":"hold","inputs":{"press":"KeyF"},
                     "attachments":[{"module":"test.mods/scatter"},{"module":"test.mods/scatter"}]}])",
                factory, seed);
        for (int press = 0; press < 3; ++press) {
            rig.engine.buttonDown(source("KeyF"), ms(press * 100));
            rig.engine.buttonUp(source("KeyF"), ms(press * 100 + 50));
        }
        REQUIRE(factory->seeds.size() == 2);
        CHECK(factory->seeds[0] != factory->seeds[1]);
        return rig.recorder.points();
    };
    const std::vector<TouchPoint> first = run(7);
    const std::vector<TouchPoint> second = run(7);
    REQUIRE(first.size() == 6);
    CHECK(first == second);
    const std::vector<TouchPoint> other = run(8);
    REQUIRE(other.size() == 6);
    CHECK_FALSE(first == other);
}

TEST_CASE("a failing attachment is left out of that call and the control keeps working (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    factory->attachments["test.mods/broken"] = {.output = [](const FingerCommand&, ModuleOutput& out) {
        out.commands[0].position.x = 0.9;
        return ModuleStatus::Failed;
    }};
    factory->attachments["test.mods/shift"] = shiftBy(0.1);
    Rig rig(R"([{"id":"fire","type":"hold","inputs":{"press":"KeyF"},
                 "attachments":[{"module":"test.mods/broken"},{"module":"test.mods/shift"}]}])",
            factory);
    rig.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(rig.recorder.last().action == TouchAction::Down);
    CHECK(rig.recorder.last().x == fixedCoordinate(0.6));
    rig.engine.buttonUp(source("KeyF"), ms(30));
    CHECK(rig.recorder.last().action == TouchAction::Up);
    CHECK(rig.recorder.last().x == fixedCoordinate(0.6));
    CHECK(rig.engine.activeTouches() == 0);
    REQUIRE(rig.factory->troubles.size() == 2);
    CHECK(rig.factory->troubles[0] == Trouble{ModuleTroubleKind::Failed, "test.mods/broken", "fire"});
}

TEST_CASE("a failing behavior lifts its fingers and stops until the profile is used again (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    int resets = 0;
    factory->behaviors["test.mods/poke"] = {
        .shape = {.slotList = {{"press", BehaviorSlotKind::Key}}, .fingers = 1, .frames = false},
        .input =
            [](const ModuleEvent& event, ModuleOutput& out) {
                if (event.kind != ModuleInput::KeyDown) {
                    return ModuleStatus::Failed;
                }
                out.commands.push_back(
                    {.action = FingerAction::Down, .finger = 0, .position = {0.3, 0.4}, .at = event.at});
                return ModuleStatus::Ok;
            },
        .frame = {},
        .wake = {},
        .resets = &resets,
    };
    Rig rig(R"([{"id":"poke","type":"behavior","behavior":"test.mods/poke","inputs":{"press":"KeyP"}}])", factory);
    CHECK(rig.engine.claims(source("KeyP")));
    rig.engine.buttonDown(source("KeyP"), ms(0));
    CHECK(rig.recorder.last().action == TouchAction::Down);
    CHECK(rig.recorder.last().x == fixedCoordinate(0.3));
    CHECK(rig.recorder.last().y == fixedCoordinate(0.4));
    CHECK(rig.engine.activeTouches() == 1);

    rig.engine.buttonUp(source("KeyP"), ms(50));
    CHECK(rig.recorder.last().action == TouchAction::Cancel);
    CHECK(rig.engine.activeTouches() == 0);
    CHECK(resets == 1);
    REQUIRE(rig.factory->troubles.size() == 1);
    CHECK(rig.factory->troubles[0] == Trouble{ModuleTroubleKind::Stopped, "test.mods/poke", "poke"});

    const std::size_t frames = rig.recorder.frames.size();
    rig.engine.buttonDown(source("KeyP"), ms(100));
    rig.engine.buttonUp(source("KeyP"), ms(150));
    CHECK(rig.recorder.frames.size() == frames);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("a behavior gets its slots, frames at the input pace while active and nothing when idle (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    std::vector<ModuleEvent> seen;
    int frames = 0;
    factory->behaviors["test.mods/spin"] = {
        .shape = {.slotList = {{"hold", BehaviorSlotKind::Key}, {"aim", BehaviorSlotKind::Axis2}},
                  .fingers = 1,
                  .frames = true},
        .input =
            [&seen](const ModuleEvent& event, ModuleOutput& out) {
                seen.push_back(event);
                if (event.kind == ModuleInput::KeyDown) {
                    out.commands.push_back(
                        {.action = FingerAction::Down, .finger = 0, .position = {0.5, 0.5}, .at = event.at});
                } else if (event.kind == ModuleInput::KeyUp) {
                    out.commands.push_back(
                        {.action = FingerAction::Up, .finger = 0, .position = {0.6, 0.5}, .at = event.at});
                }
                return ModuleStatus::Ok;
            },
        .frame =
            [&frames](Microseconds now, ModuleOutput& out) {
                ++frames;
                out.commands.push_back({.action = FingerAction::Move, .finger = 0, .position = {0.6, 0.5}, .at = now});
                return ModuleStatus::Ok;
            },
        .wake = {},
        .resets = nullptr,
    };
    Rig rig(R"([{"id":"spin","type":"behavior","behavior":"test.mods/spin","offset":[0.1,0],
                 "inputs":{"hold":"KeyQ","aim":"Gamepad.LeftStick"}}])",
            factory);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
    rig.engine.buttonDown(source("KeyQ"), ms(0));
    CHECK(rig.recorder.last().action == TouchAction::Down);
    CHECK(rig.engine.nextWakeup() == kFrameTick);
    rig.engine.tick(kFrameTick);
    CHECK(frames == 1);
    CHECK(rig.recorder.last().action == TouchAction::Move);
    CHECK(rig.recorder.last().x == fixedCoordinate(0.6));

    rig.engine.axis(source("Gamepad.LeftStick"), {0.5, 0.0}, ms(9));
    REQUIRE(seen.size() == 2);
    CHECK(seen[1].kind == ModuleInput::Axis2);
    CHECK(seen[1].slot == 1);
    CHECK(seen[1].x == 0.5);
    rig.engine.axis(source("Gamepad.LeftStick"), {0.0, 0.0}, ms(10));
    rig.engine.buttonUp(source("KeyQ"), ms(12));
    CHECK(rig.recorder.last().action == TouchAction::Up);
    CHECK(rig.engine.activeTouches() == 0);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("missing modules are reported, their attachments skipped and their behavior controls off (ADR-0105)")
{
    auto factory = std::make_shared<FakeFactory>();
    Rig rig(R"([{"id":"fire","type":"hold","inputs":{"press":"KeyF"},"attachments":[{"module":"gone.mods/scatter"}]},
                {"id":"poke","type":"behavior","behavior":"gone.mods/poke","inputs":{"press":"KeyP"}}])",
            factory);
    CHECK(rig.factory->troubles == std::vector<Trouble>{{ModuleTroubleKind::Missing, "gone.mods/scatter", "fire"},
                                                       {ModuleTroubleKind::Missing, "gone.mods/poke", "poke"}});
    CHECK_FALSE(rig.engine.claims(source("KeyP")));
    rig.engine.buttonDown(source("KeyF"), ms(0));
    CHECK(rig.recorder.last().action == TouchAction::Down);
    CHECK(rig.recorder.last().x == fixedCoordinate(0.5));
    const std::size_t frames = rig.recorder.frames.size();
    rig.engine.buttonDown(source("KeyP"), ms(10));
    CHECK(rig.recorder.frames.size() == frames);
}

TEST_CASE("an engine with idle attachments asks for no wakeup (ADR-0105, R14)")
{
    auto factory = std::make_shared<FakeFactory>();
    factory->attachments["test.mods/quiet"] = {};
    Rig rig(R"([{"id":"fire","type":"tap","inputs":{"press":"KeyF"},"attachments":[{"module":"test.mods/quiet"}]}])",
            factory);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
    rig.engine.buttonDown(source("KeyF"), ms(0));
    REQUIRE(rig.engine.nextWakeup().has_value());
    rig.engine.tick(*rig.engine.nextWakeup());
    rig.engine.buttonUp(source("KeyF"), ms(100));
    CHECK(rig.engine.activeTouches() == 0);
    CHECK_FALSE(rig.engine.nextWakeup().has_value());
}

TEST_CASE("attachments and behavior controls are read, written back unchanged and checked (ADR-0105)")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"},
         "attachments":[{"module":"community.feel-samples/randomLanding","params":{"radius":3.5,"shape":"circle"}},
                        {"module":"community.feel-samples/holdTime","enabled":false}]},
        {"id":"spin","type":"behavior","behavior":"community.feel-samples/circle","offset":[0.1,0.2],
         "inputs":{"hold":"KeyQ","aim":"Gamepad.LeftStick"},"params":{"radius":0.1,"periodMs":800}}]})");
    REQUIRE(loaded.ok());
    CHECK(std::ranges::none_of(loaded.diagnostics, [](const Diagnostic& d) { return d.severity != Severity::Info; }));
    const Control& fire = loaded.profile.controls.at(0);
    REQUIRE(fire.attachments.size() == 2);
    CHECK(fire.attachments[0].module == "community.feel-samples/randomLanding");
    CHECK(fire.attachments[0].enabled);
    CHECK(fire.attachments[0].params == R"({"radius":3.5,"shape":"circle"})");
    CHECK_FALSE(fire.attachments[1].enabled);
    CHECK(fire.attachments[1].params.empty());
    const auto& spin = std::get<BehaviorControl>(loaded.profile.controls.at(1).kind);
    CHECK(spin.behavior == "community.feel-samples/circle");
    REQUIRE(spin.inputs.size() == 2);
    CHECK(spin.inputs[0].name == "hold");
    CHECK(spin.inputs[0].binding->input == chord("KeyQ"));
    CHECK(spin.inputs[1].binding->input == chord("Gamepad.LeftStick"));
    CHECK(spin.params == R"({"radius":0.1,"periodMs":800})");

    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find("\"enabled\": false") != std::string::npos);
    CHECK(saved.find("\"enabled\": true") == std::string::npos);
    CHECK(saved.find("\"type\": \"behavior\"") != std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);
    CHECK(saveProfile(Profile{}).find("attachments") == std::string::npos);
}

TEST_CASE("the profile check names broken module references, too many entries and bad parameters (ADR-0105)")
{
    const auto check = [](std::string_view controls) {
        return loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":)" + std::string(controls) + "}")
            .diagnostics;
    };
    CHECK(has(check(R"([{"id":"a","type":"tap","attachments":[{"module":"nonsense"}]}])"), Severity::Error,
              DiagnosticCode::InvalidValue, "$.controls[0].attachments[0].module", "nonsense"));
    CHECK(has(check(R"([{"id":"a","type":"tap","attachments":[{"module":"Bad.Id/name"}]}])"), Severity::Error,
              DiagnosticCode::InvalidValue, "$.controls[0].attachments[0].module"));
    CHECK(has(check(R"([{"id":"a","type":"tap","attachments":[{"module":"ok.id/Name"}]}])"), Severity::Error,
              DiagnosticCode::InvalidValue, "$.controls[0].attachments[0].module"));
    CHECK(has(check(R"([{"id":"a","type":"tap","attachments":[{}]}])"), Severity::Error, DiagnosticCode::MissingField,
              "$.controls[0].attachments[0].module"));
    CHECK(has(check(R"([{"id":"a","type":"tap","attachments":[{"module":"ok.id/name","params":[1]}]}])"),
              Severity::Error, DiagnosticCode::WrongType, "$.controls[0].attachments[0].params"));
    CHECK(has(check(R"([{"id":"a","type":"tap","attachments":[{"module":"ok.id/name","extra":1}]}])"),
              Severity::Warning, DiagnosticCode::UnknownField, "$.controls[0].attachments[0].extra"));
    const std::string big = R"([{"id":"a","type":"tap","attachments":[{"module":"ok.id/name","params":{"text":")" +
                            std::string(kMostModuleParamBytes, 'x') + R"("}}]}])";
    CHECK(has(check(big), Severity::Error, DiagnosticCode::OutOfRange, "$.controls[0].attachments[0].params"));

    CHECK(has(check(R"([{"id":"b","type":"behavior"}])"), Severity::Error, DiagnosticCode::MissingField,
              "$.controls[0].behavior"));
    CHECK(has(check(R"([{"id":"b","type":"behavior","behavior":""}])"), Severity::Error, DiagnosticCode::InvalidValue,
              "$.controls[0].behavior"));
    CHECK(has(check(R"([{"id":"b","type":"behavior","behavior":"ok.id/spin","inputs":{"a.b":"KeyQ"}}])"),
              Severity::Error, DiagnosticCode::InvalidValue, "$.controls[0].inputs.a.b"));
    std::string slots;
    for (int index = 0; index < 9; ++index) {
        slots += (index == 0 ? "" : ",") + std::string("\"s") + std::to_string(index) + "\":\"Digit" +
                 std::to_string(index) + "\"";
    }
    CHECK(has(check(R"([{"id":"b","type":"behavior","behavior":"ok.id/spin","inputs":{)" + slots + "}}]"),
              Severity::Error, DiagnosticCode::OutOfRange, "$.controls[0].inputs", "9"));
    CHECK(has(check(R"([{"id":"b","type":"behavior","behavior":"ok.id/spin","inputs":{"hold":"KeyQ"}},
                       {"id":"c","type":"hold","inputs":{"press":"KeyQ"}}])"),
              Severity::Warning, DiagnosticCode::Conflict, "$.controls[1].inputs.press"));
}

TEST_CASE("the module check against the installed extensions names what is missing or does not fit (ADR-0105)")
{
    const Profile profile = loadControls(R"([
        {"id":"fire","type":"hold","inputs":{"press":"KeyF"},
         "attachments":[{"module":"community.feel-samples/randomLanding"},{"module":"community.feel-samples/deadZone"},
                        {"module":"other.ext/thing"}]},
        {"id":"spin","type":"behavior","behavior":"community.feel-samples/circle",
         "inputs":{"hold":"KeyQ","aim":"Gamepad.LeftStick"}},
        {"id":"spin2","type":"behavior","behavior":"community.feel-samples/circle","inputs":{"hold":"Gamepad.LeftStick"}},
        {"id":"spin3","type":"behavior","behavior":"gone.ext/spin"}])");
    ModuleCatalog catalog;
    catalog.attachments.push_back({.module = "community.feel-samples/randomLanding", .appliesTo = {"tap", "hold"}});
    catalog.attachments.push_back({.module = "community.feel-samples/deadZone", .appliesTo = {"padStick"}});
    catalog.behaviors.push_back({.module = "community.feel-samples/circle",
                                 .shape = {.slotList = {{"hold", BehaviorSlotKind::Key}}, .fingers = 1, .frames = true}});
    const std::vector<Diagnostic> found = checkModules(profile, catalog);
    CHECK(found.size() == 5);
    CHECK(has(found, Severity::Warning, DiagnosticCode::InvalidValue, "$.controls[0].attachments[1].module", "hold"));
    CHECK(has(found, Severity::Warning, DiagnosticCode::UnknownReference, "$.controls[0].attachments[2].module",
              "other.ext"));
    CHECK(has(found, Severity::Warning, DiagnosticCode::UnknownField, "$.controls[1].inputs.aim"));
    CHECK(has(found, Severity::Warning, DiagnosticCode::InputKindMismatch, "$.controls[2].inputs.hold",
              "Gamepad.LeftStick"));
    CHECK(has(found, Severity::Warning, DiagnosticCode::UnknownReference, "$.controls[3].behavior", "gone.ext"));
    CHECK(missingExtensions(profile, catalog) == std::vector<std::string>{"gone.ext", "other.ext"});
    CHECK(moduleExtension("community.feel-samples/circle") == "community.feel-samples");
    CHECK(attachmentFits({.module = "x.y/z", .appliesTo = {"*"}}, "camera"));
    CHECK(attachmentFits({.module = "x.y/z", .appliesTo = {}}, "camera"));
    CHECK_FALSE(attachmentFits({.module = "x.y/z", .appliesTo = {"tap"}}, "camera"));
}
