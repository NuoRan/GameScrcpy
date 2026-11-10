#include "ControlChannel.h"
#include "InputHost.h"

#include <gs/platform/Foreground.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../support/ThreadCpuTime.h"

using namespace gs;
using namespace gs::app;
using namespace std::chrono_literals;

namespace {

class RecordingSink final : public ControlSink
{
public:
    bool send(const protocol::TouchFrame& frame) override
    {
        if (refusals > 0) {
            --refusals;
            return false;
        }
        frames.push_back(frame);
        return true;
    }

    bool send(const protocol::Key& key) override
    {
        keys.push_back(key);
        return true;
    }

    bool send(const protocol::Scroll& scroll) override
    {
        scrolls.push_back(scroll);
        return true;
    }

    bool send(const protocol::Text& text) override
    {
        texts.push_back(text.text);
        return true;
    }

    std::vector<std::string> texts;
    std::vector<protocol::Scroll> scrolls;
    std::vector<protocol::TouchFrame> frames;
    std::vector<protocol::Key> keys;
    int refusals = 0;
};

std::shared_ptr<const input::Profile> profileOf(std::string_view json)
{
    const input::LoadResult loaded = input::loadProfile(json);
    REQUIRE(loaded.ok());
    return std::make_shared<const input::Profile>(loaded.profile);
}

std::uint8_t noButtons()
{
    return 0;
}

constexpr std::string_view kProfile = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}},
        {"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Mouse.Left"}},
        {"id":"crouch","type":"hold","anchor":"right","offset":[-0.3,0.2],"inputs":{"press":"Gamepad.A"}},
        {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}]})";

constexpr platform::RawKey kSpace{.makeCode = 0x39};
constexpr platform::RawKey kSpaceUp{.makeCode = 0x39, .released = true};
constexpr platform::RawKey kUseKey{.makeCode = 0x12};
constexpr platform::RawKey kUseKeyUp{.makeCode = 0x12, .released = true};

constexpr std::string_view kLogicProfile = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}},
        {"id":"use","type":"hold","when":"bag","inputs":{"press":"KeyE"}}],
    "vars":[{"id":"bag"}]})";

using Events = std::vector<std::pair<std::string, bool>>;

class Observer final : public LaneObserver
{
public:
    void synced(const LaneValues& values) override { syncs.push_back(values); }
    void controlPressed(std::string_view id, bool down) override { presses.emplace_back(id, down); }
    void stateChanged(std::string_view id, bool value) override { states.emplace_back(id, value); }
    void controlEnabledChanged(std::string_view id, bool on) override { enabled.emplace_back(id, on); }

    std::vector<LaneValues> syncs;
    Events presses;
    Events states;
    Events enabled;
};

std::shared_ptr<ControlChannel> streaming(RecordingSink& sink, std::uint16_t videoSession)
{
    auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(videoSession, 2400, 1080);
    return channel;
}

struct Rig {
    RecordingSink sink;
    std::shared_ptr<ControlChannel> channel = std::make_shared<ControlChannel>();
    std::uint8_t pressed = 0;
    InputHost host{{.pressedMouse = [this] { return pressed; }}};

    Rig()
    {
        channel->attach(sink);
        channel->setVideo(7, 2400, 1080);
        host.setChannel(channel, 0us);
        host.setProfile(profileOf(kProfile), 0us);
    }
};

}

TEST_CASE("the input host sends nothing until active, then touch frames for the current video session (ADR-0069)")
{
    Rig rig;
    rig.host.key(kSpace, 1000us);
    CHECK(rig.sink.frames.empty());

    rig.host.setActive(true, 2000us);
    rig.host.key(kSpace, 3000us);
    REQUIRE(rig.sink.frames.size() == 1);
    CHECK(rig.sink.frames[0].videoSession == 7);
    REQUIRE(rig.sink.frames[0].points.size() == 1);
    CHECK(rig.sink.frames[0].points[0].action == protocol::TouchAction::Down);
    CHECK(rig.host.activeTouches() == 1);

    rig.host.key(kSpaceUp, 4000us);
    REQUIRE(rig.sink.frames.size() == 2);
    CHECK(rig.sink.frames[1].points[0].action == protocol::TouchAction::Up);
    CHECK(rig.host.activeTouches() == 0);
}

TEST_CASE("a new video session releases held touches and rebuilds, a detached channel sends nothing (ADR-0069)")
{
    Rig rig;
    rig.host.setActive(true, 0us);
    rig.host.key(kSpace, 1000us);
    REQUIRE(rig.host.activeTouches() == 1);

    rig.channel->setVideo(8, 1080, 2400);
    rig.host.key({.makeCode = 0x11}, 2000us);
    REQUIRE(rig.sink.frames.size() == 3);
    CHECK(rig.sink.frames[1].videoSession == 7);
    CHECK(rig.sink.frames[1].points[0].action == protocol::TouchAction::Cancel);
    CHECK(rig.sink.frames[2].videoSession == 8);
    CHECK(rig.sink.frames[2].points[0].action == protocol::TouchAction::Down);
    CHECK(rig.host.activeTouches() == 1);

    rig.host.key(kSpaceUp, 3000us);
    REQUIRE(rig.sink.frames.size() == 4);
    CHECK(rig.sink.frames[3].points[0].action == protocol::TouchAction::Up);
    rig.host.key(kSpace, 4000us);
    REQUIRE(rig.sink.frames.size() == 5);
    CHECK(rig.sink.frames[4].videoSession == 8);
    CHECK(rig.sink.frames[4].points[0].action == protocol::TouchAction::Down);

    rig.channel->detach();
    rig.host.key(kSpaceUp, 5000us);
    rig.host.key(kSpace, 6000us);
    CHECK(rig.sink.frames.size() == 5);
    CHECK(rig.host.activeTouches() == 0);
}

TEST_CASE("deactivating releases every held touch (ADR-0069)")
{
    Rig rig;
    rig.host.setActive(true, 0us);
    rig.host.setCameraLock(input::Switch::On, 0us);
    rig.host.key(kSpace, 1000us);
    rig.host.mouse({.buttonFlags = input::rawmouse::kLeftDown}, 1500us);
    REQUIRE(rig.host.activeTouches() == 2);

    rig.host.setActive(false, 2000us);
    CHECK(rig.host.activeTouches() == 0);
    const protocol::TouchFrame& last = rig.sink.frames.back();
    CHECK(std::all_of(last.points.begin(), last.points.end(),
                      [](const protocol::TouchPoint& point) { return point.action == protocol::TouchAction::Cancel; }));

    rig.host.key(kSpaceUp, 3000us);
    rig.host.key(kSpace, 4000us);
    CHECK(rig.host.activeTouches() == 0);
}

TEST_CASE("a mouse button already down when the keymap turns on stays out until it is released (ADR-0070)")
{
    Rig rig;
    rig.pressed = platform::mouse_button::kLeft;
    rig.host.setActive(true, 0us);
    rig.host.setCameraLock(input::Switch::On, 0us);
    rig.host.mouse({.buttonFlags = input::rawmouse::kLeftUp}, 1000us);
    CHECK(rig.host.activeTouches() == 0);
    CHECK(rig.sink.frames.empty());

    rig.host.mouse({.buttonFlags = input::rawmouse::kLeftDown}, 2000us);
    CHECK(rig.host.activeTouches() == 1);
    rig.host.mouse({.buttonFlags = input::rawmouse::kLeftUp}, 3000us);
    CHECK(rig.host.activeTouches() == 0);
}

TEST_CASE("mouse buttons and gamepad buttons reach their targets (ADR-0069)")
{
    Rig rig;
    rig.host.setActive(true, 0us);
    rig.host.setCameraLock(input::Switch::On, 0us);
    rig.host.mouse({.buttonFlags = input::rawmouse::kLeftDown}, 1000us);
    CHECK(rig.host.activeTouches() == 1);
    rig.host.mouse({.buttonFlags = input::rawmouse::kLeftUp}, 2000us);
    CHECK(rig.host.activeTouches() == 0);

    rig.host.gamepad({.connected = true, .buttons = input::xinput::kA}, 3000us);
    CHECK(rig.host.activeTouches() == 1);
    rig.host.gamepad({.connected = false}, 4000us);
    CHECK(rig.host.activeTouches() == 0);
}

TEST_CASE("the cursor touches through the engine even without a profile, and scrolls carry the video session (ADR-0069)")
{
    RecordingSink sink;
    const auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(7, 2400, 1080);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);

    host.cursor(input::CursorPhase::Down, {0.25, 0.75}, 1000us);
    host.cursor(input::CursorPhase::Up, {0.25, 0.75}, 2000us);
    REQUIRE(sink.frames.size() == 2);
    CHECK(sink.frames[0].videoSession == 7);
    REQUIRE(sink.frames[0].points.size() == 1);
    CHECK(sink.frames[0].points[0].action == protocol::TouchAction::Down);
    CHECK(sink.frames[0].points[0].x == 16384);
    CHECK(sink.frames[0].points[0].y == 49151);
    CHECK(sink.frames[1].points[0].action == protocol::TouchAction::Up);
    CHECK(host.activeTouches() == 0);

    CHECK(channel->scroll({0.5, 0.5}, -256, 512));
    REQUIRE(sink.scrolls.size() == 1);
    CHECK(sink.scrolls[0].videoSession == 7);
    CHECK(sink.scrolls[0].x == 32768);
    CHECK(sink.scrolls[0].y == 32768);
    CHECK(sink.scrolls[0].hScroll == -256);
    CHECK(sink.scrolls[0].vScroll == 512);

    channel->detach();
    CHECK_FALSE(channel->scroll({0.5, 0.5}, 0, 256));
    CHECK(sink.scrolls.size() == 1);
}

TEST_CASE("typed text goes out in pieces of at most 4096 bytes without splitting a character (ADR-0069)")
{
    RecordingSink sink;
    ControlChannel channel;
    CHECK_FALSE(channel.type("a"));
    channel.attach(sink);
    CHECK(channel.type("hello"));
    const std::string run(4095, 'a');
    CHECK(channel.type(run + "\xE4\xB8\xAD" + "b"));
    REQUIRE(sink.texts.size() == 3);
    CHECK(sink.texts[0] == "hello");
    CHECK(sink.texts[1] == run);
    CHECK(sink.texts[2] == "\xE4\xB8\xAD" "b");
}

TEST_CASE("mouse bindings act only while the camera is locked, the lock survives a new engine and ends with the device (ADR-0070)")
{
    constexpr std::string_view kShooter = R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Mouse.Left"}},
                    {"id":"look","type":"camera","lockKey":"Tab","inputs":{"look":"Mouse.Move"}}]})";
    constexpr std::string_view kNoCamera = R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}}]})";
    constexpr platform::RawKey kTab{.makeCode = 0x0F};
    constexpr platform::RawKey kTabUp{.makeCode = 0x0F, .released = true};
    constexpr platform::RawMouse kPress{.buttonFlags = input::rawmouse::kLeftDown};
    constexpr platform::RawMouse kRelease{.buttonFlags = input::rawmouse::kLeftUp};

    RecordingSink sink;
    const auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(7, 2400, 1080);
    std::vector<input::PointerState> pointers;
    std::vector<input::EngineNotice> notices;
    InputHost host{{.pointer = [&pointers](input::PointerState state) { pointers.push_back(state); },
                    .notice = [&notices](input::EngineNotice notice) { notices.push_back(notice); },
                    .pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kShooter), 0us);
    host.setActive(true, 0us);

    host.mouse(kPress, 1000us);
    CHECK(host.activeTouches() == 0);
    host.mouse(kRelease, 2000us);

    host.key(kTab, 3000us);
    host.key(kTabUp, 3500us);
    REQUIRE(pointers == std::vector{input::PointerState::Captured});
    CHECK(host.locked());
    host.mouse(kPress, 4000us);
    CHECK(host.activeTouches() == 1);
    host.mouse(kRelease, 5000us);
    CHECK(host.activeTouches() == 0);

    channel->setVideo(8, 2400, 1080);
    host.mouse(kPress, 6000us);
    CHECK(host.activeTouches() == 1);
    CHECK(host.locked());
    host.mouse(kRelease, 7000us);

    host.setProfile(profileOf(kNoCamera), 8000us);
    host.mouse(kPress, 9000us);
    CHECK_FALSE(host.locked());
    CHECK(pointers.back() == input::PointerState::Free);
    CHECK(notices.empty());
    CHECK(host.activeTouches() == 0);
    host.mouse(kRelease, 9500us);

    host.setProfile(profileOf(kShooter), 10000us);
    host.setCameraLock(input::Switch::On, 11000us);
    CHECK(host.locked());
    host.setChannel(nullptr, 12000us);
    CHECK_FALSE(host.locked());
    CHECK(pointers.back() == input::PointerState::Free);
}

TEST_CASE("a key held while its profile is rebuilt keeps working in the new engine; taps, the lock key and another "
          "profile do not get it (issue 11)")
{
    constexpr std::string_view kBefore = R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"move","type":"wasd","inputs":{"up":"KeyW"}},
                    {"id":"jump","type":"tap","anchor":"right","offset":[-0.2,0],"inputs":{"press":"Space"}},
                    {"id":"look","type":"camera","lockKey":"Tab","inputs":{"look":"Mouse.Move"}}]})";
    constexpr std::string_view kAfter = R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"move","type":"wasd","radius":0.2,"inputs":{"up":"KeyW"}},
                    {"id":"jump","type":"tap","anchor":"right","offset":[-0.2,0],"inputs":{"press":"Space"}},
                    {"id":"look","type":"camera","lockKey":"Tab","inputs":{"look":"Mouse.Move"}}]})";
    constexpr platform::RawKey kW{.makeCode = 0x11};
    constexpr platform::RawKey kWUp{.makeCode = 0x11, .released = true};
    constexpr platform::RawKey kTab{.makeCode = 0x0F};

    RecordingSink sink;
    const auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(7, 2400, 1080);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kBefore), 0us);
    host.setActive(true, 0us);

    host.key(kW, 1000us);
    CHECK(host.activeTouches() == 1);
    host.key(kTab, 1500us);
    CHECK(host.locked());
    host.key(kSpace, 2000us);
    CHECK(host.activeTouches() == 2);

    host.setProfile(profileOf(kAfter), 3000us, true);
    CHECK(host.activeTouches() == 1);
    CHECK(host.locked());
    host.key(kWUp, 4000us);
    CHECK(host.activeTouches() == 0);

    host.key(kW, 5000us);
    CHECK(host.activeTouches() == 1);
    host.setProfile(profileOf(kBefore), 6000us);
    CHECK(host.activeTouches() == 0);
}

TEST_CASE("a temporary cursor brings the cursor out while locked, and leaving the window or the phone puts it back (ADR-0094)")
{
    constexpr std::string_view kShooter = R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Mouse.Left"}},
                    {"id":"look","type":"camera","lockKey":"Tab","inputs":{"look":"Mouse.Move"}},
                    {"id":"pointer","type":"tempCursor","inputs":{"press":"KeyC"}}]})";
    constexpr platform::RawKey kC{.makeCode = 0x2E};
    constexpr platform::RawKey kCUp{.makeCode = 0x2E, .released = true};
    constexpr platform::RawMouse kPress{.buttonFlags = input::rawmouse::kLeftDown};
    constexpr platform::RawMouse kRelease{.buttonFlags = input::rawmouse::kLeftUp};

    RecordingSink sink;
    const auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(7, 2400, 1080);
    std::vector<input::PointerState> pointers;
    std::vector<bool> peeks;
    InputHost host{{.pointer = [&pointers](input::PointerState state) { pointers.push_back(state); },
                    .pressedMouse = noButtons,
                    .peek = [&peeks](bool on) { peeks.push_back(on); }}};
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kShooter), 0us);
    host.setActive(true, 0us);
    host.key(kC, 500us);
    CHECK_FALSE(host.peeking());
    host.key(kCUp, 600us);
    host.setCameraLock(input::Switch::On, 1000us);
    REQUIRE(host.locked());

    host.key(kC, 2000us);
    CHECK(host.peeking());
    CHECK(host.locked());
    CHECK(peeks == std::vector<bool>{true});
    host.mouse(kPress, 3000us);
    CHECK(host.activeTouches() == 0);
    host.mouse(kRelease, 3500us);
    host.key(kCUp, 4000us);
    CHECK_FALSE(host.peeking());
    CHECK(peeks == std::vector<bool>{true, false});
    host.mouse(kPress, 5000us);
    CHECK(host.activeTouches() == 1);
    host.mouse(kRelease, 5500us);

    host.key(kC, 6000us);
    CHECK(host.peeking());
    host.setActive(false, 7000us);
    CHECK_FALSE(host.peeking());
    CHECK(host.locked());
    CHECK(peeks == std::vector<bool>{true, false, true, false});
    host.setActive(true, 8000us);
    host.mouse(kPress, 9000us);
    CHECK(host.activeTouches() == 1);
    host.mouse(kRelease, 9500us);

    host.key(kC, 10000us);
    CHECK(host.peeking());
    host.setChannel(nullptr, 11000us);
    CHECK_FALSE(host.peeking());
    CHECK_FALSE(host.locked());
    CHECK(peeks.back() == false);
    CHECK(pointers == std::vector{input::PointerState::Captured, input::PointerState::Free});
}

TEST_CASE("in cursor state a claimed mouse button goes to its binding instead of the normal cursor click (ADR-0077)")
{
    constexpr std::string_view kMoba = R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"pick","type":"hold","anchor":"right","offset":[-0.3,0],
                     "inputs":{"press":{"input":"Mouse.Left","claim":"cursor"}}},
                    {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}]})";
    RecordingSink sink;
    const auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(7, 2400, 1080);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kMoba), 0us);
    host.setActive(true, 0us);
    REQUIRE_FALSE(host.locked());

    host.mouse({.buttonFlags = input::rawmouse::kMiddleDown}, 3000us);
    host.mouse({.buttonFlags = input::rawmouse::kMiddleUp}, 4000us);
    CHECK(sink.keys.empty());
    CHECK(sink.frames.empty());

    host.cursor(input::CursorPhase::Down, {0.1, 0.1}, 5000us);
    REQUIRE(sink.frames.size() == 1);
    CHECK(sink.frames[0].points[0].action == protocol::TouchAction::Down);
    CHECK(sink.frames[0].points[0].x > 32768);
    host.cursor(input::CursorPhase::Up, {0.1, 0.1}, 6000us);
    CHECK(host.activeTouches() == 0);

    host.mouse({.buttonFlags = input::rawmouse::kLeftDown}, 7000us);
    host.mouse({.buttonFlags = input::rawmouse::kLeftUp}, 8000us);
    CHECK(sink.frames.size() == 2);
}

TEST_CASE("each phone keeps its own engine and states, switching away releases the keys held on it (ADR-0070)")
{
    const auto profile = profileOf(kLogicProfile);
    RecordingSink sinkA;
    RecordingSink sinkB;
    const auto a = streaming(sinkA, 7);
    const auto b = streaming(sinkB, 3);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(a, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    const auto watch = std::make_shared<Observer>();
    host.attach(a, profile, watch, 0us);
    CHECK(host.setState(a.get(), "bag", true, 1000us));
    host.key(kSpace, 3000us);
    REQUIRE(host.activeTouches() == 1);

    host.setChannel(b, 4000us);
    host.setProfile(profile, 4000us);
    CHECK(sinkA.frames.back().points[0].action == protocol::TouchAction::Cancel);
    CHECK(host.activeTouches() == 0);
    host.key(kSpaceUp, 5000us);
    host.key(kUseKey, 6000us);
    host.key(kUseKeyUp, 6500us);
    CHECK(sinkB.frames.empty());
    CHECK(host.laneCount() == 2);

    host.setChannel(a, 8000us);
    host.setProfile(profile, 8000us);
    const std::size_t before = sinkA.frames.size();
    host.key(kUseKey, 9000us);
    REQUIRE(sinkA.frames.size() == before + 1);
    CHECK(sinkA.frames.back().points[0].action == protocol::TouchAction::Down);
    host.key(kUseKeyUp, 9500us);

    host.forget(b.get(), 10000us);
    CHECK(host.laneCount() == 1);
    host.forget(a.get(), 11000us);
    CHECK(host.laneCount() == 0);
    CHECK(host.activeTouches() == 0);
}

TEST_CASE("logic drives a phone in the background with its own pointers and hears only its own phone (ADR-0081)")
{
    const auto profile = profileOf(kLogicProfile);
    RecordingSink sinkA;
    RecordingSink sinkB;
    const auto a = streaming(sinkA, 7);
    const auto b = streaming(sinkB, 3);
    const auto watchA = std::make_shared<Observer>();
    const auto watchB = std::make_shared<Observer>();
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(a, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    host.attach(a, profile, watchA, 0us);
    host.attach(b, profile, watchB, 0us);
    REQUIRE(watchA->syncs.size() == 1);
    REQUIRE(watchB->syncs.size() == 1);
    CHECK(watchB->syncs[0] == LaneValues{.states = {{"bag", false}}, .enabled = {{"jump", true}, {"use", true}}});

    host.key(kSpace, 1000us);
    host.key(kSpaceUp, 2000us);
    CHECK(watchA->presses == Events{{"jump", true}, {"jump", false}});
    CHECK(watchB->presses.empty());

    CHECK(host.pressControl(b.get(), "jump", true, 3000us));
    REQUIRE(sinkB.frames.size() == 1);
    CHECK(sinkB.frames[0].videoSession == 3);
    CHECK(sinkB.frames[0].points[0].action == protocol::TouchAction::Down);
    CHECK(sinkB.frames[0].points[0].pointerId < InputHost::kEnginePointers);
    CHECK(host.pressControl(b.get(), "jump", false, 4000us));
    CHECK(sinkB.frames.back().points[0].action == protocol::TouchAction::Up);
    CHECK(watchB->presses.empty());
    CHECK_FALSE(host.pressControl(b.get(), "nope", true, 4500us));

    host.tap(b.get(), 1, {0.25, 0.5}, 30ms, 5000us);
    REQUIRE(sinkB.frames.size() == 3);
    CHECK(sinkB.frames[2].points[0].action == protocol::TouchAction::Down);
    CHECK(sinkB.frames[2].points[0].pointerId == InputHost::kEnginePointers);
    CHECK(sinkB.frames[2].videoSession == 3);
    CHECK(host.nextWakeup() == 35000us);
    host.wake(35000us);
    REQUIRE(sinkB.frames.size() == 4);
    CHECK(sinkB.frames[3].points[0].action == protocol::TouchAction::Up);
    CHECK(sinkB.frames[3].points[0].pointerId == InputHost::kEnginePointers);
    CHECK(sinkB.frames[3].points[0].x == sinkB.frames[2].points[0].x);

    host.swipe(b.get(), 2, {0.1, 0.5}, {0.9, 0.5}, 50ms, 40000us);
    REQUIRE(sinkB.frames.size() == 5);
    CHECK(sinkB.frames[4].points[0].pointerId == InputHost::kEnginePointers + 1);
    while (const std::optional<Microseconds> due = host.nextWakeup()) {
        host.wake(*due);
    }
    REQUIRE(sinkB.frames.size() == 9);
    std::uint16_t x = sinkB.frames[4].points[0].x;
    for (std::size_t i = 5; i < 8; ++i) {
        CHECK(sinkB.frames[i].points[0].action == protocol::TouchAction::Move);
        CHECK(sinkB.frames[i].points[0].x > x);
        x = sinkB.frames[i].points[0].x;
    }
    CHECK(sinkB.frames[8].points[0].action == protocol::TouchAction::Up);
    CHECK(sinkB.frames[8].points[0].x == input::fixedCoordinate(0.9));
    CHECK(sinkA.frames.size() == 2);

    host.androidKey(b.get(), 4, true);
    host.androidKey(b.get(), 4, false);
    REQUIRE(sinkB.keys.size() == 2);
    CHECK(sinkB.keys[0].action == protocol::KeyAction::Down);

    host.detach(b.get(), watchB.get(), 100000us);
    CHECK_FALSE(host.pressControl(b.get(), "jump", true, 101000us));
    const std::size_t sent = sinkB.frames.size();
    host.tap(b.get(), 3, {0.5, 0.5}, 10ms, 102000us);
    host.androidKey(b.get(), 4, true);
    CHECK(sinkB.frames.size() == sent);
    CHECK(sinkB.keys.size() == 2);
}

TEST_CASE("a script lift the phone's queue refuses is sent again, and its pointer stays taken until then (ADR-0081)")
{
    const auto profile = profileOf(kLogicProfile);
    RecordingSink sink;
    const auto channel = streaming(sink, 7);
    const auto watch = std::make_shared<Observer>();
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    host.attach(channel, profile, watch, 0us);

    host.tap(channel.get(), 4, {0.25, 0.5}, 10ms, 1000us);
    REQUIRE(sink.frames.size() == 1);
    CHECK(sink.frames[0].points[0].pointerId == InputHost::kEnginePointers);
    sink.refusals = 1;
    host.wake(11000us);
    CHECK(sink.frames.size() == 1);
    CHECK(host.nextWakeup() == 15000us);

    host.tap(channel.get(), 5, {0.75, 0.5}, 10ms, 12000us);
    REQUIRE(sink.frames.size() == 2);
    CHECK(sink.frames[1].points[0].action == protocol::TouchAction::Down);
    CHECK(sink.frames[1].points[0].pointerId == InputHost::kEnginePointers + 1);
    CHECK(host.nextWakeup() == 15000us);

    host.wake(15000us);
    REQUIRE(sink.frames.size() == 3);
    CHECK(sink.frames[2].points[0].action == protocol::TouchAction::Up);
    CHECK(sink.frames[2].points[0].pointerId == InputHost::kEnginePointers);
    CHECK(sink.frames[2].points[0].x == sink.frames[0].points[0].x);
    CHECK(host.nextWakeup() == 22000us);
    host.wake(22000us);
    REQUIRE(sink.frames.size() == 4);
    CHECK(sink.frames[3].points[0].action == protocol::TouchAction::Up);
    CHECK(sink.frames[3].points[0].pointerId == InputHost::kEnginePointers + 1);
    CHECK_FALSE(host.nextWakeup().has_value());

    host.tap(channel.get(), 6, {0.5, 0.5}, 10ms, 30000us);
    sink.refusals = 1;
    host.detach(channel.get(), watch.get(), 31000us);
    CHECK(sink.frames.size() == 5);
    CHECK(host.nextWakeup() == 35000us);
    host.wake(35000us);
    REQUIRE(sink.frames.size() == 6);
    CHECK(sink.frames[5].points[0].action == protocol::TouchAction::Cancel);
    CHECK_FALSE(host.nextWakeup().has_value());
}

TEST_CASE("a script gesture lifted early lifts its finger where it is now and frees its pointer")
{
    const auto profile = profileOf(kLogicProfile);
    RecordingSink sink;
    const auto channel = streaming(sink, 7);
    const auto watch = std::make_shared<Observer>();
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    host.attach(channel, profile, watch, 0us);

    host.tap(channel.get(), 11, {0.25, 0.5}, 10s, 1000us);
    host.swipe(channel.get(), 12, {0.1, 0.5}, {0.9, 0.5}, 100ms, 2000us);
    REQUIRE(sink.frames.size() == 2);
    host.liftGesture(channel.get(), 11, 3000us);
    REQUIRE(sink.frames.size() == 3);
    CHECK(sink.frames[2].points[0].action == protocol::TouchAction::Up);
    CHECK(sink.frames[2].points[0].pointerId == InputHost::kEnginePointers);
    CHECK(sink.frames[2].points[0].x == sink.frames[0].points[0].x);
    host.liftGesture(channel.get(), 11, 3500us);
    host.liftGesture(channel.get(), 99, 3500us);
    CHECK(sink.frames.size() == 3);

    host.liftGesture(channel.get(), 12, 52000us);
    REQUIRE(sink.frames.size() == 4);
    CHECK(sink.frames[3].points[0].action == protocol::TouchAction::Up);
    CHECK(sink.frames[3].points[0].pointerId == InputHost::kEnginePointers + 1);
    CHECK(sink.frames[3].points[0].x == input::fixedCoordinate(0.5));
    CHECK_FALSE(host.nextWakeup().has_value());
}

TEST_CASE("states and switches survive a new video session quietly, and logic pauses while another profile is tried (ADR-0081)")
{
    const auto profile = profileOf(kLogicProfile);
    const auto trial = profileOf(kLogicProfile);
    RecordingSink sink;
    const auto channel = streaming(sink, 7);
    const auto watch = std::make_shared<Observer>();
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    host.attach(channel, profile, watch, 0us);
    CHECK(host.setState(channel.get(), "bag", true, 1000us));
    CHECK(watch->states == Events{{"bag", true}});
    CHECK(host.enableControl(channel.get(), "jump", false, 2000us));
    CHECK(watch->enabled == Events{{"jump", false}});

    channel->setVideo(8, 1080, 2400);
    host.key(kUseKey, 3000us);
    REQUIRE_FALSE(sink.frames.empty());
    CHECK(sink.frames.back().videoSession == 8);
    CHECK(sink.frames.back().points[0].action == protocol::TouchAction::Down);
    host.key(kUseKeyUp, 3500us);
    host.key(kSpace, 4000us);
    CHECK(host.activeTouches() == 0);
    host.key(kSpaceUp, 4500us);
    CHECK(watch->states.size() == 1);
    CHECK(watch->enabled.size() == 1);
    CHECK(watch->presses == Events{{"use", true}, {"use", false}});

    host.setProfile(trial, 5000us);
    CHECK_FALSE(host.pressControl(channel.get(), "use", true, 5500us));
    const std::size_t sent = sink.frames.size();
    host.tap(channel.get(), 7, {0.5, 0.5}, 10ms, 5600us);
    host.androidKey(channel.get(), 4, true);
    CHECK(sink.frames.size() == sent);
    CHECK(sink.keys.empty());
    host.androidKey(channel.get(), 4, false);
    REQUIRE(sink.keys.size() == 1);
    CHECK(sink.keys[0].action == protocol::KeyAction::Up);
    host.key(kSpace, 6000us);
    CHECK(host.activeTouches() == 1);
    host.key(kSpaceUp, 6500us);
    CHECK(watch->presses.size() == 2);
    CHECK(watch->syncs.size() == 1);

    host.setProfile(profile, 7000us);
    REQUIRE(watch->syncs.size() == 2);
    CHECK(watch->syncs[1] == LaneValues{.states = {{"bag", false}}, .enabled = {{"jump", true}, {"use", true}}});
    CHECK(host.pressControl(channel.get(), "jump", true, 7500us));
}

TEST_CASE("logic's writes before the picture starts are kept for the phone's engine (ADR-0081)")
{
    const auto profile = profileOf(kLogicProfile);
    RecordingSink sink;
    const auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    const auto watch = std::make_shared<Observer>();
    InputHost host{{.pressedMouse = noButtons}};
    host.attach(channel, profile, watch, 0us);
    REQUIRE(watch->syncs.size() == 1);
    CHECK(host.setState(channel.get(), "bag", true, 1000us));
    CHECK(host.setState(channel.get(), "bag", true, 1100us));
    CHECK_FALSE(host.setState(channel.get(), "nope", true, 1200us));
    CHECK(host.enableControl(channel.get(), "jump", false, 1250us));
    CHECK(watch->states == Events{{"bag", true}});
    CHECK(watch->enabled == Events{{"jump", false}});
    CHECK_FALSE(host.pressControl(channel.get(), "use", true, 1300us));
    host.tap(channel.get(), 8, {0.5, 0.5}, 10ms, 1400us);
    CHECK(sink.frames.empty());

    channel->setVideo(5, 2400, 1080);
    host.setChannel(channel, 2000us);
    host.setProfile(profile, 2000us);
    host.setActive(true, 2000us);
    host.key(kUseKey, 3000us);
    REQUIRE(sink.frames.size() == 1);
    CHECK(sink.frames[0].videoSession == 5);
    CHECK(sink.frames[0].points[0].action == protocol::TouchAction::Down);
    host.key(kUseKeyUp, 3500us);
    host.key(kSpace, 4000us);
    CHECK(sink.frames.size() == 2);
    CHECK(watch->states.size() == 1);
    CHECK(watch->enabled.size() == 1);
}

TEST_CASE("a new refresh rate keeps the fingers that are down and only changes the pacing (RC1 D-003)")
{
    Rig rig;
    rig.host.setActive(true, 0us);
    rig.host.key(kSpace, 1000us);
    REQUIRE(rig.host.activeTouches() == 1);
    const std::size_t sent = rig.sink.frames.size();

    rig.channel->setRefreshHz(120.0);
    rig.channel->setRefreshHz(120.0);
    rig.channel->setRefreshHz(90.0);
    rig.host.key({.makeCode = 0x11}, 2000us);
    rig.host.mouse({.dx = 4, .dy = 2}, 2500us);
    CHECK(rig.host.activeTouches() == 1);
    CHECK(rig.sink.frames.size() == sent);

    rig.host.key(kSpaceUp, 3000us);
    REQUIRE(rig.sink.frames.size() == sent + 1);
    CHECK(rig.sink.frames.back().points[0].action == protocol::TouchAction::Up);
    CHECK(rig.sink.frames.back().videoSession == 7);
    CHECK(rig.host.activeTouches() == 0);
}

namespace {

constexpr std::string_view kPlayProfile = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],
         "inputs":{"up":"KeyW","left":"KeyA","down":"KeyS","right":"KeyD"}},
        {"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}},
        {"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Mouse.Left"}},
        {"id":"burst","type":"turbo","anchor":"right","offset":[-0.4,0.1],"inputs":{"press":"KeyR"},"rateHz":12},
        {"id":"peek","type":"freeLook","anchor":"topRight","offset":[-0.2,0.2],
         "inputs":{"hold":"Mouse.Right","look":"Mouse.Move"}},
        {"id":"look","type":"camera","lockKey":"Backquote","inputs":{"look":"Mouse.Move"}}]})";

struct Phone {
    std::uint16_t session = 7;
    std::uint32_t down = 0;
    int downTwice = 0;

    void take(const protocol::TouchFrame& frame)
    {
        const bool stale = frame.videoSession != session;
        for (const protocol::TouchPoint& point : frame.points) {
            const std::uint32_t bit = 1U << point.pointerId;
            if (point.action == protocol::TouchAction::Up || point.action == protocol::TouchAction::Cancel) {
                down &= ~bit;
            } else if (!stale && point.action == protocol::TouchAction::Down) {
                downTwice += (down & bit) != 0 ? 1 : 0;
                down |= bit;
            }
        }
    }
};

}

TEST_CASE("random play never leaves a finger on the phone or a key that no longer presses (RC1 D-003)")
{
    constexpr std::array<std::uint16_t, 8> kKeys{0x11, 0x1E, 0x1F, 0x20, 0x39, 0x13, 0x29, 0x01};
    constexpr std::array<double, 3> kRefresh{60.0, 90.0, 120.0};
    for (std::uint32_t seed = 1; seed <= 12; ++seed) {
        CAPTURE(seed);
        std::mt19937 random(seed);
        const auto below = [&random](std::uint32_t bound) { return static_cast<std::uint32_t>(random() % bound); };
        Rig rig;
        rig.host.setProfile(profileOf(kPlayProfile), 0us);
        Phone phone;
        std::size_t seen = 0;
        const auto pump = [&] {
            for (; seen < rig.sink.frames.size(); ++seen) {
                phone.take(rig.sink.frames[seen]);
            }
        };
        Microseconds now = 0us;
        bool active = false;
        bool touching = false;
        bool portrait = false;
        std::uint8_t buttons = 0;
        std::uint16_t clientSession = 7;
        std::set<std::uint16_t> held;
        const auto learnSession = [&] {
            if (clientSession != phone.session) {
                clientSession = phone.session;
                portrait = below(2) == 0;
                rig.channel->setVideo(clientSession, portrait ? 1080 : 2400, portrait ? 2400 : 1080);
            }
        };
        for (int step = 0; step < 3000; ++step) {
            now += Microseconds{500 + below(16'000)};
            const std::uint32_t pick = below(100);
            if (pick < 34) {
                const std::uint16_t code = kKeys[below(static_cast<std::uint32_t>(kKeys.size()))];
                const bool release = held.erase(code) > 0;
                if (!release) {
                    held.insert(code);
                }
                rig.host.key({.makeCode = code, .released = release}, now);
            } else if (pick < 44) {
                const bool right = below(2) == 0;
                const std::uint8_t bit = right ? platform::mouse_button::kRight : platform::mouse_button::kLeft;
                const bool release = (buttons & bit) != 0;
                buttons = static_cast<std::uint8_t>(buttons ^ bit);
                const std::uint16_t flags = right ? (release ? input::rawmouse::kRightUp : input::rawmouse::kRightDown)
                                                  : (release ? input::rawmouse::kLeftUp : input::rawmouse::kLeftDown);
                rig.host.mouse({.buttonFlags = flags}, now);
            } else if (pick < 64) {
                rig.host.mouse({.dx = static_cast<std::int32_t>(below(81)) - 40,
                                .dy = static_cast<std::int32_t>(below(81)) - 40},
                               now);
            } else if (pick < 71) {
                const input::Vec2 at{0.05 + 0.9 * below(1000) / 1000.0, 0.05 + 0.9 * below(1000) / 1000.0};
                if (!touching) {
                    touching = true;
                    rig.host.cursor(input::CursorPhase::Down, at, now);
                } else if (below(3) == 0) {
                    touching = false;
                    rig.host.cursor(below(4) == 0 ? input::CursorPhase::Cancel : input::CursorPhase::Up, at, now);
                } else {
                    rig.host.cursor(input::CursorPhase::Move, at, now);
                }
            } else if (pick < 75) {
                active = !active;
                rig.pressed = buttons;
                rig.host.setActive(active, now);
            } else if (pick < 77) {
                ++phone.session;
            } else if (pick < 81) {
                learnSession();
            } else if (pick < 87) {
                rig.channel->setRefreshHz(kRefresh[below(static_cast<std::uint32_t>(kRefresh.size()))]);
            } else if (pick < 89) {
                rig.sink.refusals = 1 + static_cast<int>(below(4));
                phone.down = 0;
                ++phone.session;
            } else if (pick < 91) {
                rig.host.setCameraLock(input::Switch::Toggle, now);
            } else {
                rig.host.wake(now);
            }
            pump();
        }

        for (const std::uint16_t code : held) {
            rig.host.key({.makeCode = code, .released = true}, now);
        }
        held.clear();
        if ((buttons & platform::mouse_button::kLeft) != 0) {
            rig.host.mouse({.buttonFlags = input::rawmouse::kLeftUp}, now);
        }
        if ((buttons & platform::mouse_button::kRight) != 0) {
            rig.host.mouse({.buttonFlags = input::rawmouse::kRightUp}, now);
        }
        buttons = 0;
        if (touching) {
            rig.host.cursor(input::CursorPhase::Up, {0.5, 0.5}, now);
        }
        learnSession();
        rig.sink.refusals = 0;
        now += 2s;
        rig.host.wake(now);
        rig.host.setCameraLock(input::Switch::Off, now);
        rig.pressed = 0;
        rig.host.setActive(true, now);
        rig.host.setActive(false, now + 1ms);
        pump();
        CHECK(phone.down == 0);
        CHECK(rig.host.activeTouches() == 0);

        rig.host.setActive(true, now + 2ms);
        rig.host.key(kSpace, now + 3ms);
        pump();
        CHECK(std::popcount(phone.down) == 1);
        rig.host.key(kSpaceUp, now + 4ms);
        pump();
        CHECK(phone.down == 0);
        CHECK(phone.downTwice == 0);
    }
}

TEST_CASE("an input event becomes a touch frame on the input thread in well under a millisecond (INP-005)")
{
    Rig rig;
    rig.host.setProfile(profileOf(kPlayProfile), 0us);
    rig.host.setActive(true, 0us);
    rig.host.setCameraLock(input::Switch::On, 0us);
    constexpr std::array<std::uint16_t, 5> kKeys{0x11, 0x1E, 0x1F, 0x20, 0x39};
    const gs::testing::ThreadCpuTime clock;
    REQUIRE(clock.usable());
    std::mt19937 random(7);
    std::vector<double> spentUs;
    spentUs.reserve(20'000);
    std::set<std::uint16_t> held;
    Microseconds now = 1000us;
    for (int i = 0; i < 20'000; ++i) {
        now += Microseconds{250 + random() % 2000};
        const std::uint64_t start = clock.now();
        if (random() % 3 == 0) {
            const std::uint16_t code = kKeys[random() % kKeys.size()];
            const bool release = held.erase(code) > 0;
            if (!release) {
                held.insert(code);
            }
            rig.host.key({.makeCode = code, .released = release}, now);
        } else {
            rig.host.mouse({.dx = static_cast<std::int32_t>(random() % 31) - 15,
                            .dy = static_cast<std::int32_t>(random() % 31) - 15},
                           now);
        }
        rig.host.wake(now);
        spentUs.push_back(clock.micros(start, clock.now()));
    }
    std::ranges::sort(spentUs);
    const double p50 = spentUs[spentUs.size() / 2];
    const double p99 = spentUs[spentUs.size() * 99 / 100];
    MESSAGE("input event to touch frame, thread CPU time: p50 " << p50 << " us, p99 " << p99 << " us, max "
                                                               << spentUs.back() << " us over "
                                                               << rig.sink.frames.size() << " frames");
    CHECK_FALSE(rig.sink.frames.empty());
    CHECK(p99 < 1000.0);
}

TEST_CASE("keys the logic listens to reach it, fingers it puts down stay until lifted, and it drives components (ADR-0091)")
{
    class Watcher final : public LaneObserver
    {
    public:
        void synced(const LaneValues&) override {}
        void controlPressed(std::string_view, bool) override {}
        void stateChanged(std::string_view, bool) override {}
        void controlEnabledChanged(std::string_view, bool) override {}
        void keyEvent(std::string_view name, bool down) override { keys.emplace_back(name, down); }
        void statusChanged(std::string_view id, const input::ControlStatus& status) override
        {
            touching.emplace_back(id, status.touching);
        }
        void lockChanged(bool locked) override { locks.push_back(locked); }

        Events keys;
        Events touching;
        std::vector<bool> locks;
    };
    const auto profile = profileOf(kLogicProfile);
    RecordingSink sink;
    const auto channel = streaming(sink, 7);
    const auto watcher = std::make_shared<Watcher>();
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    host.attach(channel, profile, watcher, 0us);

    host.key(kUseKey, 1000us);
    host.key(kUseKeyUp, 2000us);
    CHECK(watcher->keys.empty());
    host.claimKeys(channel.get(), {*input::parseSource("KeyE"), *input::parseSource("Space")});
    host.key(kUseKey, 3000us);
    host.key(kUseKey, 3500us);
    host.key(kSpace, 4000us);
    host.key(kUseKeyUp, 5000us);
    CHECK(watcher->keys == Events{{"KeyE", true}, {"Space", true}, {"KeyE", false}});
    CHECK(watcher->touching == Events{{"jump", true}});
    host.claimKeys(channel.get(), {*input::parseSource("KeyE")});
    CHECK(watcher->keys.back() == std::pair<std::string, bool>{"Space", false});

    const std::size_t before = sink.frames.size();
    host.holdGesture(channel.get(), 9, {0.4, 0.5}, 6000us);
    REQUIRE(sink.frames.size() == before + 1);
    CHECK(sink.frames.back().points[0].action == protocol::TouchAction::Down);
    host.wake(60000us);
    CHECK(sink.frames.size() == before + 1);
    host.moveGesture(channel.get(), 9, {0.6, 0.5}, 0us, 61000us);
    REQUIRE(sink.frames.size() == before + 2);
    CHECK(sink.frames.back().points[0].action == protocol::TouchAction::Move);
    CHECK(sink.frames.back().points[0].x == input::fixedCoordinate(0.6));
    host.liftAll(channel.get(), 62000us);
    REQUIRE(sink.frames.size() == before + 3);
    CHECK(sink.frames.back().points[0].action == protocol::TouchAction::Up);

    host.typeText(channel.get(), "gg");
    CHECK(sink.texts == std::vector<std::string>{"gg"});
    CHECK(host.withEngine(
        channel.get(), [](input::MappingEngine& engine) { return engine.resetControl("jump", 63000us); }, 63000us));
    CHECK(watcher->touching.back() == std::pair<std::string, bool>{"jump", false});
    CHECK_FALSE(host.withEngine(
        channel.get(), [](input::MappingEngine& engine) { return engine.resetControl("nope", 64000us); }, 64000us));
}

TEST_CASE("a task watching a phone hears its components and its own keys without taking them, and follows a new "
          "profile (ADR-0091)")
{
    class Listener final : public LaneObserver
    {
    public:
        void synced(const LaneValues&) override { ++syncs; }
        void controlPressed(std::string_view id, bool down) override { presses.emplace_back(id, down); }
        void stateChanged(std::string_view, bool) override {}
        void controlEnabledChanged(std::string_view, bool) override {}
        void keyEvent(std::string_view name, bool down) override { keys.emplace_back(name, down); }
        void profileChanged(const std::shared_ptr<const input::Profile>& profile) override
        {
            profiles.push_back(profile);
        }

        int syncs = 0;
        Events presses;
        Events keys;
        std::vector<std::shared_ptr<const input::Profile>> profiles;
    };
    const auto profile = profileOf(kLogicProfile);
    RecordingSink sink;
    const auto channel = streaming(sink, 7);
    const auto task = std::make_shared<Listener>();
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(channel, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    host.watch(channel, profile, task, 0us);
    CHECK(task->syncs == 1);
    CHECK(task->profiles.empty());

    host.watchKeys(channel.get(), task.get(), {*input::parseSource("Space")});
    host.key(kSpace, 1000us);
    host.key(kSpaceUp, 2000us);
    CHECK(task->keys == Events{{"Space", true}, {"Space", false}});
    CHECK(task->presses == Events{{"jump", true}, {"jump", false}});
    CHECK(std::ranges::any_of(sink.frames, [](const protocol::TouchFrame& frame) {
        return !frame.points.empty() && frame.points[0].action == protocol::TouchAction::Down;
    }));
    CHECK(host.pressControl(channel.get(), "jump", true, 3000us));
    CHECK(host.pressControl(channel.get(), "jump", false, 3100us));

    const auto other = profileOf(kProfile);
    host.setProfile(other, 4000us);
    REQUIRE(task->profiles.size() == 1);
    CHECK(task->profiles[0] == other);
    CHECK(task->syncs == 2);

    host.key(kSpace, 5000us);
    CHECK(task->keys.back() == std::pair<std::string, bool>{"Space", true});
    host.unwatch(channel.get(), task.get(), 6000us);
    CHECK(task->keys.back() == std::pair<std::string, bool>{"Space", false});
    const std::size_t heard = task->keys.size();
    host.key(kSpaceUp, 7000us);
    host.key(kSpace, 8000us);
    CHECK(task->keys.size() == heard);
    CHECK_FALSE(host.pressControl(channel.get(), "jump", true, 9000us));
}

namespace {

constexpr std::string_view kFollowerProfile = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"leap","type":"hold","anchor":"topLeft","offset":[0.25,0.25],"inputs":{"press":"Space"}},
        {"id":"shoot","type":"hold","anchor":"left","offset":[0.3,0],
         "inputs":{"press":{"input":"Mouse.X1","claim":"both"}}},
        {"id":"zoom","type":"hold","anchor":"top","offset":[0,0.3],
         "inputs":{"press":{"input":"Mouse.WheelUp","claim":"both"}}}]})";

constexpr std::string_view kBareProfile = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[{"id":"use","type":"hold","inputs":{"press":"KeyE"}}]})";

std::shared_ptr<ControlChannel> portrait(RecordingSink& sink, std::uint16_t videoSession, std::uint16_t width,
                                         std::uint16_t height)
{
    auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(videoSession, width, height);
    return channel;
}

std::size_t downs(const RecordingSink& sink)
{
    return static_cast<std::size_t>(std::ranges::count_if(sink.frames, [](const protocol::TouchFrame& frame) {
        return !frame.points.empty() && frame.points[0].action == protocol::TouchAction::Down;
    }));
}

bool allCancelled(const protocol::TouchFrame& frame)
{
    return !frame.points.empty() && std::ranges::all_of(frame.points, [](const protocol::TouchPoint& point) {
        return point.action == protocol::TouchAction::Cancel;
    });
}

}

TEST_CASE("a master's keys, mouse buttons and wheel reach every follower, each by its own profile (ADR-0096)")
{
    RecordingSink sinkA;
    RecordingSink sinkB;
    RecordingSink sinkC;
    const auto a = streaming(sinkA, 7);
    const auto b = portrait(sinkB, 3, 1080, 2400);
    const auto c = streaming(sinkC, 5);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(a, 0us);
    host.setProfile(profileOf(kProfile), 0us);
    host.setActive(true, 0us);
    CHECK(host.setFollowers(a,
                            {{.channel = b, .profile = profileOf(kFollowerProfile)},
                             {.channel = c, .profile = profileOf(kBareProfile)}},
                            0us) == 2);
    CHECK(host.followerCount(a.get()) == 2);
    CHECK(host.leaderOf(b.get()) == a.get());
    CHECK(host.leaderOf(a.get()) == nullptr);

    host.key(kSpace, 1000us);
    REQUIRE(sinkA.frames.size() == 1);
    REQUIRE(sinkB.frames.size() == 1);
    CHECK(sinkA.frames[0].points[0].x > 32768);
    CHECK(sinkB.frames[0].videoSession == 3);
    CHECK(sinkB.frames[0].points[0].action == protocol::TouchAction::Down);
    CHECK(sinkB.frames[0].points[0].x < 32768);
    CHECK(sinkB.frames[0].points[0].y < 32768);
    host.key(kSpaceUp, 2000us);
    REQUIRE(sinkB.frames.size() == 2);
    CHECK(sinkB.frames[1].points[0].action == protocol::TouchAction::Up);

    host.mouse({.buttonFlags = input::rawmouse::kX1Down}, 3000us);
    CHECK(sinkA.frames.size() == 2);
    REQUIRE(sinkB.frames.size() == 3);
    CHECK(sinkB.frames[2].points[0].action == protocol::TouchAction::Down);
    host.mouse({.buttonFlags = input::rawmouse::kX1Up}, 3500us);
    CHECK(sinkB.frames.back().points[0].action == protocol::TouchAction::Up);

    const std::size_t before = downs(sinkB);
    host.mouse({.buttonFlags = input::rawmouse::kWheel, .buttonData = 120}, 4000us);
    CHECK(downs(sinkB) == before + 1);
    CHECK(sinkA.frames.size() == 2);
    CHECK(sinkC.frames.empty());
}

TEST_CASE("a tap on the picture lands on every follower at the same proportional place, whatever its screen "
          "(ADR-0096)")
{
    RecordingSink sinkA;
    RecordingSink sinkB;
    const auto a = streaming(sinkA, 7);
    const auto b = portrait(sinkB, 4, 720, 1280);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(a, 0us);
    host.setProfile(profileOf(kProfile), 0us);
    REQUIRE(host.setFollowers(a, {{.channel = b, .profile = profileOf(kBareProfile)}}, 0us) == 1);

    host.cursor(input::CursorPhase::Down, {0.25, 0.75}, 1000us);
    host.cursor(input::CursorPhase::Move, {0.5, 0.5}, 1500us);
    host.cursor(input::CursorPhase::Up, {0.5, 0.5}, 2000us);
    REQUIRE(sinkA.frames.size() >= 2);
    REQUIRE(sinkB.frames.size() == sinkA.frames.size());
    CHECK(sinkB.frames[0].videoSession == 4);
    REQUIRE(sinkB.frames[0].points.size() == 1);
    CHECK(sinkB.frames[0].points[0].action == protocol::TouchAction::Down);
    CHECK(sinkB.frames[0].points[0].x == 16384);
    CHECK(sinkB.frames[0].points[0].y == 49151);
    CHECK(sinkB.frames[0].points[0].x == sinkA.frames[0].points[0].x);
    CHECK(sinkB.frames.back().points.back().action == protocol::TouchAction::Up);
    CHECK(sinkB.frames.back().points.back().x == input::fixedCoordinate(0.5));
    CHECK(sinkB.frames.back().points.back().y == input::fixedCoordinate(0.5));
}

TEST_CASE("when the master's window loses focus or another phone takes the keys, followers let go too (ADR-0096)")
{
    RecordingSink sinkA;
    RecordingSink sinkB;
    RecordingSink sinkC;
    const auto a = streaming(sinkA, 7);
    const auto b = streaming(sinkB, 3);
    const auto c = streaming(sinkC, 5);
    const auto profile = profileOf(kProfile);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(a, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    REQUIRE(host.setFollowers(a, {{.channel = b, .profile = profile}}, 0us) == 1);

    host.key(kSpace, 1000us);
    REQUIRE(sinkB.frames.size() == 1);
    host.setActive(false, 2000us);
    REQUIRE(sinkB.frames.size() == 2);
    CHECK(allCancelled(sinkB.frames.back()));
    host.key(kSpaceUp, 2500us);
    host.key(kSpace, 3000us);
    CHECK(sinkB.frames.size() == 2);
    host.key(kSpaceUp, 3500us);

    host.setActive(true, 4000us);
    host.key(kSpace, 5000us);
    REQUIRE(sinkB.frames.size() == 3);
    CHECK(sinkB.frames.back().points[0].action == protocol::TouchAction::Down);
    host.setChannel(c, 6000us);
    REQUIRE(sinkB.frames.size() == 4);
    CHECK(allCancelled(sinkB.frames.back()));
    host.setProfile(profile, 6000us);
    host.key(kSpaceUp, 6500us);
    host.key(kSpace, 7000us);
    CHECK(sinkB.frames.size() == 4);
    CHECK_FALSE(sinkC.frames.empty());
}

TEST_CASE("stopping the sync lets go on the followers, and the master's keys stay on the master (ADR-0096)")
{
    RecordingSink sinkA;
    RecordingSink sinkB;
    const auto a = streaming(sinkA, 7);
    const auto b = streaming(sinkB, 3);
    const auto profile = profileOf(kProfile);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(a, 0us);
    host.setProfile(profile, 0us);
    host.setActive(true, 0us);
    REQUIRE(host.setFollowers(a, {{.channel = b, .profile = profile}}, 0us) == 1);
    host.key(kSpace, 1000us);
    REQUIRE(sinkB.frames.size() == 1);

    CHECK(host.setFollowers(a, {}, 2000us) == 0);
    REQUIRE(sinkB.frames.size() == 2);
    CHECK(allCancelled(sinkB.frames.back()));
    CHECK(host.followerCount(a.get()) == 0);
    CHECK(host.leaderOf(b.get()) == nullptr);

    const std::size_t master = sinkA.frames.size();
    host.key(kSpaceUp, 3000us);
    host.key(kSpace, 4000us);
    CHECK(sinkB.frames.size() == 2);
    CHECK(sinkA.frames.size() == master + 2);
}

TEST_CASE("a phone follows one master at a time, and a master cannot follow another phone (ADR-0096)")
{
    RecordingSink sinkA;
    RecordingSink sinkB;
    RecordingSink sinkC;
    const auto a = streaming(sinkA, 7);
    const auto b = streaming(sinkB, 3);
    const auto c = streaming(sinkC, 5);
    const auto profile = profileOf(kProfile);
    InputHost host{{.pressedMouse = noButtons}};
    REQUIRE(host.setFollowers(a, {{.channel = b, .profile = profile}}, 0us) == 1);
    CHECK(host.setFollowers(c, {{.channel = b, .profile = profile}}, 1000us) == 0);
    CHECK(host.leaderOf(b.get()) == a.get());
    CHECK(host.followerCount(c.get()) == 0);

    CHECK(host.setFollowers(c, {{.channel = a, .profile = profile}}, 2000us) == 0);
    CHECK(host.leaderOf(a.get()) == nullptr);
    CHECK(host.setFollowers(b, {{.channel = c, .profile = profile}}, 3000us) == 0);
    CHECK(host.followerCount(b.get()) == 0);
    CHECK(host.leaderOf(c.get()) == nullptr);

    CHECK(host.setFollowers(a,
                            {{.channel = b, .profile = profile},
                             {.channel = b, .profile = profile},
                             {.channel = a, .profile = profile},
                             {.channel = c, .profile = profile}},
                            4000us) == 2);
    CHECK(host.followerCount(a.get()) == 2);

    CHECK(host.setFollowers(a, {}, 5000us) == 0);
    CHECK(host.setFollowers(c, {{.channel = b, .profile = profile}}, 6000us) == 1);
    CHECK(host.leaderOf(b.get()) == c.get());
    host.forget(c.get(), 7000us);
    CHECK(host.leaderOf(b.get()) == nullptr);
    CHECK(host.followerCount(a.get()) == 0);
}

TEST_CASE("the lock key reaches followers like any key, and locking from the window locks them too (ADR-0096)")
{
    class LockWatcher final : public LaneObserver
    {
    public:
        void synced(const LaneValues&) override {}
        void controlPressed(std::string_view, bool) override {}
        void stateChanged(std::string_view, bool) override {}
        void controlEnabledChanged(std::string_view, bool) override {}
        void lockChanged(bool locked) override { locks.push_back(locked); }

        std::vector<bool> locks;
    };
    constexpr std::string_view kShooter = R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Mouse.Left"}},
                    {"id":"look","type":"camera","lockKey":"Tab","inputs":{"look":"Mouse.Move"}}]})";
    constexpr platform::RawKey kTab{.makeCode = 0x0F};
    constexpr platform::RawKey kTabUp{.makeCode = 0x0F, .released = true};
    constexpr platform::RawMouse kPress{.buttonFlags = input::rawmouse::kLeftDown};
    constexpr platform::RawMouse kRelease{.buttonFlags = input::rawmouse::kLeftUp};

    RecordingSink sinkA;
    RecordingSink sinkB;
    const auto a = streaming(sinkA, 7);
    const auto b = streaming(sinkB, 3);
    const auto master = profileOf(kShooter);
    const auto follower = profileOf(kShooter);
    InputHost host{{.pressedMouse = noButtons}};
    host.setChannel(a, 0us);
    host.setProfile(master, 0us);
    host.setActive(true, 0us);
    REQUIRE(host.setFollowers(a, {{.channel = b, .profile = follower}}, 0us) == 1);
    const auto watcher = std::make_shared<LockWatcher>();
    host.attach(b, follower, watcher, 0us);

    host.key(kTab, 1000us);
    host.key(kTabUp, 1500us);
    REQUIRE(host.locked());
    CHECK(watcher->locks == std::vector<bool>{true});
    std::size_t before = downs(sinkB);
    host.mouse(kPress, 2000us);
    CHECK(downs(sinkB) == before + 1);
    host.mouse(kRelease, 2500us);

    host.setCameraLock(input::Switch::Off, 3000us);
    CHECK_FALSE(host.locked());
    CHECK(watcher->locks == std::vector<bool>{true, false});
    before = downs(sinkB);
    host.mouse(kPress, 4000us);
    CHECK(downs(sinkB) == before);
    host.mouse(kRelease, 4500us);

    host.setCameraLock(input::Switch::On, 5000us);
    CHECK(host.locked());
    CHECK(watcher->locks == std::vector<bool>{true, false, true});
    before = downs(sinkB);
    host.mouse(kPress, 6000us);
    CHECK(downs(sinkB) == before + 1);
    host.mouse(kRelease, 6500us);

    const std::size_t sent = sinkB.frames.size();
    host.mouse({.dx = 12, .dy = 3}, 100000us);
    CHECK(sinkB.frames.size() > sent);
}
