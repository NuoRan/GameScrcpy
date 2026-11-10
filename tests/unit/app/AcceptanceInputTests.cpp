#include "ControlChannel.h"
#include "InputController.h"
#include "InputHost.h"

#include <gs/input/DeviceEvents.h>
#include <gs/platform/Foreground.h>

#include <doctest/doctest.h>

#include <QRectF>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

using namespace gs;
using namespace gs::app;
using namespace std::chrono_literals;

namespace {

class FrameSink final : public ControlSink
{
public:
    bool send(const protocol::TouchFrame& frame) override
    {
        const std::lock_guard lock(m_mutex);
        m_frames.push_back(frame);
        return true;
    }
    bool send(const protocol::Key&) override { return true; }
    bool send(const protocol::Scroll&) override { return true; }
    bool send(const protocol::Text&) override { return true; }

    [[nodiscard]] std::vector<protocol::TouchFrame> frames() const
    {
        const std::lock_guard lock(m_mutex);
        return m_frames;
    }

    [[nodiscard]] std::size_t count() const
    {
        const std::lock_guard lock(m_mutex);
        return m_frames.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<protocol::TouchFrame> m_frames;
};

std::shared_ptr<const input::Profile> profileOf(std::string_view json)
{
    const input::LoadResult loaded = input::loadProfile(json);
    REQUIRE(loaded.ok());
    return std::make_shared<const input::Profile>(loaded.profile);
}

std::shared_ptr<ControlChannel> streaming(FrameSink& sink)
{
    auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(7, 2400, 1080);
    return channel;
}

bool lockedOnInputThread(InputController& controller)
{
    const auto answer = std::make_shared<std::promise<bool>>();
    std::future<bool> locked = answer->get_future();
    controller.post([answer](InputHost& host, Microseconds) { answer->set_value(host.locked()); });
    REQUIRE(locked.wait_for(5s) == std::future_status::ready);
    return locked.get();
}

constexpr std::uintptr_t kNeverInFront = 1;
constexpr int kStallEvents = 200;
constexpr platform::RawKey kSpace{.makeCode = 0x39};
constexpr platform::RawKey kSpaceUp{.makeCode = 0x39, .released = true};
constexpr platform::RawKey kKeyF{.makeCode = 0x21};
constexpr platform::RawKey kKeyL{.makeCode = 0x26};
constexpr platform::RawKey kKeyLUp{.makeCode = 0x26, .released = true};
constexpr platform::RawKey kKeyG{.makeCode = 0x22};
constexpr platform::RawKey kKeyGUp{.makeCode = 0x22, .released = true};

constexpr std::string_view kShooter = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}},
        {"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Mouse.Left"}},
        {"id":"look","type":"camera","lockKey":"Tab","inputs":{"look":"Mouse.Move"}}]})";

constexpr std::string_view kRacer = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"boost","type":"hold","anchor":"center","offset":[0,0],"inputs":{"press":"Space"}},
        {"id":"brake","type":"hold","anchor":"left","offset":[0.3,0],"inputs":{"press":"KeyF"}}]})";

constexpr std::string_view kTimed = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"jump","type":"hold","inputs":{"press":"Space"}},
        {"id":"bag","type":"longPress","offset":[0.2,0],"inputs":{"press":"KeyL"},"holdMs":300},
        {"id":"spray","type":"turbo","offset":[0.3,0],"inputs":{"press":"KeyG"},"rateHz":10}]})";

void wakeUntilIdle(InputHost& host, Microseconds from)
{
    Microseconds now = from;
    for (int i = 0; i < 200; ++i) {
        const std::optional<Microseconds> due = host.nextWakeup();
        if (!due) {
            return;
        }
        now = std::max(now, *due);
        host.wake(now);
    }
}

}

TEST_CASE("INP-007: a profile loaded while mirroring takes the next key press at once and lets go of what the last one held")
{
    FrameSink sink;
    const auto channel = streaming(sink);
    InputHost host{{.pressedMouse = [] { return std::uint8_t{0}; }}};
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kShooter), 0us);
    host.setActive(true, 0us);

    host.key(kSpace, 1000us);
    REQUIRE(host.activeTouches() == 1);
    const protocol::TouchPoint first = sink.frames().back().points.back();
    CHECK(first.action == protocol::TouchAction::Down);

    host.setProfile(profileOf(kRacer), 2000us);
    CHECK(host.activeTouches() == 0);
    CHECK(sink.frames().back().points.back().action == protocol::TouchAction::Cancel);

    host.key(kSpaceUp, 3000us);
    host.key(kSpace, 4000us);
    REQUIRE(host.activeTouches() == 1);
    const protocol::TouchPoint second = sink.frames().back().points.back();
    CHECK(second.action == protocol::TouchAction::Down);
    CHECK(second.x == input::fixedCoordinate(0.5));
    CHECK(second.y == input::fixedCoordinate(0.5));
    CHECK(second.x != first.x);

    host.key(kKeyF, 5000us);
    CHECK(host.activeTouches() == 2);
}

TEST_CASE("INP-004, INP-017: leaving the window lets go of every finger and key, keeps the lock, and the click that brings it "
          "back does nothing")
{
    FrameSink sink;
    const auto channel = streaming(sink);
    std::uint8_t pressed = 0;
    std::vector<input::PointerState> pointers;
    InputHost host{{.pointer = [&pointers](input::PointerState state) { pointers.push_back(state); },
                    .pressedMouse = [&pressed] { return pressed; }}};
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kShooter), 0us);
    host.setActive(true, 0us);
    host.setCameraLock(input::Switch::On, 0us);
    REQUIRE(host.locked());
    host.key(kSpace, 1000us);
    host.mouse({.buttonFlags = input::rawmouse::kLeftDown}, 1100us);
    REQUIRE(host.activeTouches() == 2);

    host.setActive(false, 2000us);
    CHECK(host.activeTouches() == 0);
    const std::vector<protocol::TouchFrame> frames = sink.frames();
    REQUIRE_FALSE(frames.empty());
    for (const protocol::TouchPoint& point : frames.back().points) {
        CHECK(point.action == protocol::TouchAction::Cancel);
    }
    CHECK(host.locked());

    const std::size_t afterLeaving = sink.count();
    host.key(kSpaceUp, 2100us);
    host.key(kSpace, 2200us);
    host.key(kSpaceUp, 2300us);
    CHECK(sink.count() == afterLeaving);

    pressed = platform::mouse_button::kLeft;
    host.setActive(true, 3000us);
    CHECK(host.locked());
    host.mouse({.buttonFlags = input::rawmouse::kLeftUp}, 3100us);
    CHECK(host.activeTouches() == 0);
    CHECK(sink.count() == afterLeaving);

    pressed = 0;
    host.mouse({.buttonFlags = input::rawmouse::kLeftDown}, 3200us);
    CHECK(host.activeTouches() == 1);
    host.mouse({.buttonFlags = input::rawmouse::kLeftUp}, 3300us);
    CHECK(host.activeTouches() == 0);
    CHECK(pointers == std::vector{input::PointerState::Captured});
}

TEST_CASE("INP-017: typing lets go of the camera lock, and finishing typing does not lock it again")
{
    FrameSink sink;
    const auto channel = streaming(sink);
    InputController controller;
    controller.setTarget(QStringLiteral("A"), channel, kNeverInFront);
    controller.setFocused(true);
    controller.setProfile(profileOf(kShooter));
    REQUIRE(controller.engaged());
    controller.setCameraLock(input::Switch::On);
    CHECK(lockedOnInputThread(controller));

    controller.setTextInput(true);
    CHECK(controller.textInput());
    CHECK_FALSE(controller.engaged());
    CHECK_FALSE(lockedOnInputThread(controller));

    controller.setTextInput(false);
    CHECK_FALSE(controller.textInput());
    CHECK(controller.engaged());
    CHECK_FALSE(lockedOnInputThread(controller));

    controller.setCameraLock(input::Switch::On);
    CHECK(lockedOnInputThread(controller));
}

TEST_CASE("INP-005: with the interface thread held up for 200 ms, input still becomes touch frames on the input thread")
{
    FrameSink sink;
    const auto channel = streaming(sink);
    InputController controller;
    controller.setTarget(QStringLiteral("A"), channel, kNeverInFront);
    controller.setFocused(true);
    controller.setProfile(profileOf(kRacer));
    REQUIRE(controller.engaged());
    CHECK_FALSE(lockedOnInputThread(controller));
    const std::size_t before = sink.count();

    const auto latencyUs = std::make_shared<std::vector<double>>(static_cast<std::size_t>(kStallEvents), -1.0);
    const auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    std::thread device([&controller, latencyUs, done] {
        auto next = std::chrono::steady_clock::now();
        for (int i = 0; i < kStallEvents; ++i) {
            next += 500us;
            while (std::chrono::steady_clock::now() < next) {
                std::this_thread::yield();
            }
            const auto posted = std::chrono::steady_clock::now();
            const std::uint16_t code = (i / 2) % 2 == 0 ? std::uint16_t{0x39} : std::uint16_t{0x21};
            const bool released = i % 2 == 1;
            controller.post([latencyUs, done, i, posted, code, released](InputHost& host, Microseconds now) {
                host.key({.makeCode = code, .released = released}, now);
                (*latencyUs)[static_cast<std::size_t>(i)] =
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - posted).count();
                if (i == kStallEvents - 1) {
                    done->set_value();
                }
            });
        }
    });

    const auto stallEnd = std::chrono::steady_clock::now() + 200ms;
    while (std::chrono::steady_clock::now() < stallEnd) {
        std::this_thread::yield();
    }
    const std::size_t framesDuringStall = sink.count() - before;
    device.join();
    REQUIRE(finished.wait_for(5s) == std::future_status::ready);

    std::vector<double> sorted = *latencyUs;
    std::ranges::sort(sorted);
    const double p50 = sorted[sorted.size() / 2];
    const double p99 = sorted[sorted.size() * 99 / 100];
    MESSAGE("input event to touch frame through the input thread while the interface thread was busy: p50 "
            << p50 << " us, p99 " << p99 << " us, max " << sorted.back() << " us; " << framesDuringStall
            << " of " << kStallEvents << " frames sent during the 200 ms");
    CHECK(sorted.front() >= 0.0);
    CHECK(framesDuringStall > 0);
    CHECK(sink.count() - before >= static_cast<std::size_t>(kStallEvents));
    CHECK(p50 < 1000.0);
    CHECK(p99 < 20'000.0);
}

TEST_CASE("INP-014: an idle input host asks for no wakeup, and a long press or a turbo asks only until it is done")
{
    FrameSink sink;
    const auto channel = streaming(sink);
    InputHost host{{.pressedMouse = [] { return std::uint8_t{0}; }}};
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kTimed), 0us);
    host.setActive(true, 0us);
    CHECK_FALSE(host.nextWakeup().has_value());

    host.key(kSpace, 1000us);
    host.key(kSpaceUp, 2000us);
    wakeUntilIdle(host, 2000us);
    CHECK_FALSE(host.nextWakeup().has_value());
    CHECK(host.activeTouches() == 0);

    host.key(kKeyL, 10'000us);
    const std::optional<Microseconds> due = host.nextWakeup();
    REQUIRE(due.has_value());
    CHECK(*due > 10'000us);
    host.key(kKeyLUp, 20'000us);
    CHECK(host.activeTouches() == 1);
    wakeUntilIdle(host, 20'000us);
    CHECK(host.activeTouches() == 0);
    CHECK_FALSE(host.nextWakeup().has_value());

    host.key(kKeyG, 400'000us);
    CHECK(host.nextWakeup().has_value());
    host.key(kKeyGUp, 410'000us);
    wakeUntilIdle(host, 410'000us);
    CHECK(host.activeTouches() == 0);
    CHECK_FALSE(host.nextWakeup().has_value());
}

TEST_CASE("SCR-019: logic reads no held keys and no cursor while the mirror window is not in front or another phone has them")
{
    FrameSink sink;
    const auto channel = streaming(sink);
    InputController controller;
    controller.setTarget(QStringLiteral("A"), channel, kNeverInFront);
    controller.setProfile(profileOf(kShooter));
    controller.setPictureArea(QStringLiteral("A"), QRectF(0.0, 0.0, 1.0, 1.0));

    CHECK_FALSE(controller.cursorOver(QStringLiteral("A")).has_value());
    CHECK_FALSE(controller.heldOn(QStringLiteral("A"), "KeyF"));
    CHECK_FALSE(controller.heldOn(QStringLiteral("A"), "Mouse.Left"));

    controller.setFocused(true);
    CHECK_FALSE(controller.cursorOver(QStringLiteral("B")).has_value());
    CHECK_FALSE(controller.heldOn(QStringLiteral("B"), "KeyF"));
    CHECK_FALSE(controller.heldOn(QStringLiteral("B"), "Mouse.Left"));

    controller.setFocused(false);
    CHECK_FALSE(controller.cursorOver(QStringLiteral("A")).has_value());
    CHECK_FALSE(controller.heldOn(QStringLiteral("A"), "Space"));
}
