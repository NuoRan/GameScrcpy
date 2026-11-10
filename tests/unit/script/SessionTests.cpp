#include "gs/script/Session.h"

#include <doctest/doctest.h>

#include <string>
#include <variant>

using gs::Microseconds;
using gs::script::DeviceInfo;
using gs::script::parseSession;
using gs::script::Session;
using gs::script::SessionEvent;
using gs::script::SessionEventKind;
using gs::script::SessionKey;
using gs::script::SessionOperation;
using gs::script::SessionScroll;
using gs::script::SessionText;
using gs::script::SessionTouch;
using gs::script::SessionTouchAction;
using gs::script::SessionTouchPoint;
using gs::script::writeSession;

namespace {

void checkSame(const Session& left, const Session& right)
{
    CHECK(left.device == right.device);
    CHECK(left.startedAtMs == right.startedAtMs);
    CHECK(left.frameRate == right.frameRate);
    CHECK(left.profile == right.profile);
    CHECK(left.events == right.events);
    CHECK(left.video == right.video);
    CHECK(left.audio == right.audio);
    CHECK(left.videoStartPtsUs == right.videoStartPtsUs);
    CHECK(left.durationMs == right.durationMs);
    CHECK(left.operations == right.operations);
}

Session recorded()
{
    Session session;
    session.device = DeviceInfo{"Pixel 8 Pro", 15, 1080, 2400, 1};
    session.startedAtMs = 1'793'534'400'123;
    session.frameRate = 60.0;
    session.profile = "../profiles/shooter.json";
    session.video = "video.media";
    session.audio = "audio.media";
    session.videoStartPtsUs = 4'788'849'286;
    session.durationMs = 133'250;
    session.events = {
        SessionEvent{Microseconds{1'000}, SessionEventKind::Press, "fire", false},
        SessionEvent{Microseconds{1'500}, SessionEventKind::State, "scoped", true},
        SessionEvent{Microseconds{2'250}, SessionEventKind::Release, "fire", false},
        SessionEvent{Microseconds{3'000}, SessionEventKind::Run, "reload", false},
        SessionEvent{Microseconds{4'000}, SessionEventKind::Answer, R"({"choice":1})", false},
    };
    session.operations = {
        SessionOperation{Microseconds{1'200'000},
                         SessionTouch{{SessionTouchPoint{0, SessionTouchAction::Down, 0.5, 0.25},
                                       SessionTouchPoint{1, SessionTouchAction::Move, 0.12345, 1.0}}}},
        SessionOperation{Microseconds{1'250'500}, SessionKey{true, 4, 1, 0}},
        SessionOperation{Microseconds{1'300'000}, SessionKey{false, 4, 1, 2}},
        SessionOperation{Microseconds{1'400'000}, SessionScroll{0.75, 0.5, 0, -2}},
        SessionOperation{Microseconds{1'500'000}, SessionText{5}},
        SessionOperation{Microseconds{1'600'000}, SessionTouch{{SessionTouchPoint{0, SessionTouchAction::Up, 0.5, 0.25}}}},
    };
    return session;
}

}

TEST_CASE("sessions: version 1 files still load, with no recording")
{
    const auto session = parseSession(R"({"format": "gamescrcpy.session", "version": 1,
        "device": {"model": "Pixel", "android": 14, "width": 1080, "height": 2340},
        "events": [{"at": 10, "press": "a"}, {"at": 20, "release": "a"}],
        "operations": [{"at": 1, "text": {"length": 3}}], "video": "video.media"})");
    REQUIRE(session.has_value());
    CHECK(session->device == DeviceInfo{"Pixel", 14, 1080, 2340, 0});
    CHECK(session->events.size() == 2);
    CHECK(session->video.empty());
    CHECK(session->audio.empty());
    CHECK(session->videoStartPtsUs == 0);
    CHECK(session->durationMs == 0);
    CHECK(session->operations.empty());
}

TEST_CASE("sessions: version 2 adds the streams, the first frame's time, the length and the operations")
{
    const auto session = parseSession(R"({"format": "gamescrcpy.session", "version": 2,
        "video": "video.media", "audio": "audio.media", "videoStartPtsUs": 4788849286, "durationMs": 9000,
        "events": [{"at": 30, "press": "fire"}],
        "operations": [
            {"at": 2000, "scroll": {"x": 0.5, "y": 0.5, "h": 0, "v": -1}},
            {"at": 1200, "text": {"length": 5}},
            {"at": 100, "touch": {"points": [{"id": 0, "action": "down", "x": 0.25, "y": 0.75}]}},
            {"at": 150.5, "key": {"action": "up", "keycode": 66}}
        ]})");
    REQUIRE(session.has_value());
    CHECK(session->video == "video.media");
    CHECK(session->audio == "audio.media");
    CHECK(session->videoStartPtsUs == 4'788'849'286);
    CHECK(session->durationMs == 9'000);
    REQUIRE(session->operations.size() == 4);
    CHECK(session->operations[0]
          == SessionOperation{Microseconds{100'000}, SessionTouch{{SessionTouchPoint{0, SessionTouchAction::Down, 0.25, 0.75}}}});
    CHECK(session->operations[1] == SessionOperation{Microseconds{150'500}, SessionKey{false, 66, 0, 0}});
    CHECK(session->operations[2] == SessionOperation{Microseconds{1'200'000}, SessionText{5}});
    CHECK(session->operations[3] == SessionOperation{Microseconds{2'000'000}, SessionScroll{0.5, 0.5, 0, -1}});
}

TEST_CASE("sessions: writing a session and reading it back gives the same session, as version 2")
{
    const Session session = recorded();
    const std::string text = writeSession(session);
    CHECK(text.find(R"("version": 2)") != std::string::npos);
    CHECK(text.find(R"("startedAt": "2026-11-01T12:00:00.123Z")") != std::string::npos);
    CHECK(text.find(R"({"at":1500,"text":{"length":5}})") != std::string::npos);
    CHECK(text.find(R"("answer":{"choice":1})") != std::string::npos);
    const auto parsed = parseSession(text);
    REQUIRE(parsed.has_value());
    checkSame(*parsed, session);
    CHECK(writeSession(*parsed) == text);

    Session empty;
    empty.device = DeviceInfo{"Phone", 15, 1080, 2400, 0};
    empty.startedAtMs = gs::script::kDefaultSessionStartMs;
    const auto reread = parseSession(writeSession(empty));
    REQUIRE(reread.has_value());
    checkSame(*reread, empty);
}

TEST_CASE("sessions: a session with no known screen is still written so that it reads back")
{
    Session session;
    session.device = DeviceInfo{"Phone", 120, 0, 70'000, 9};
    session.frameRate = 1'000.0;
    session.video = "../video.media";
    const auto parsed = parseSession(writeSession(session));
    REQUIRE(parsed.has_value());
    CHECK(parsed->device == DeviceInfo{"Phone", 15, 1080, 2400, 0});
    CHECK(parsed->frameRate == 240.0);
    CHECK(parsed->video.empty());
}

TEST_CASE("sessions: broken recordings are named by their path")
{
    const struct {
        const char* text;
        const char* problem;
    } broken[] = {
        {R"({"format": "gamescrcpy.session", "version": 0})", "$.version: must be 1 or 2"},
        {R"({"format": "gamescrcpy.session", "version": 2, "video": "../video.media"})", "$.video:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "audio": 5})", "$.audio:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "videoStartPtsUs": 1.5})", "$.videoStartPtsUs:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "durationMs": -1})", "$.durationMs:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": {}})", "$.operations:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1}]})",
         "$.operations[0]: needs exactly one"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1, "text": {"length": 1}, "key": {}}]})",
         "$.operations[0]: needs exactly one"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"text": {"length": 1}}]})",
         "$.operations[0].at:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1, "text": {"value": "secret"}}]})",
         "$.operations[0].text.length:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1, "touch": {"points": [{"id": 0, "action": "down", "x": 1.5, "y": 0}]}}]})",
         "$.operations[0].touch.points[0].x:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1, "touch": {"points": [{"id": 40, "action": "down", "x": 0, "y": 0}]}}]})",
         "$.operations[0].touch.points[0].id:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1, "touch": {"points": [{"id": 0, "action": "hover", "x": 0, "y": 0}]}}]})",
         "$.operations[0].touch.points[0].action:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1, "key": {"action": "down"}}]})",
         "$.operations[0].key.keycode:"},
        {R"({"format": "gamescrcpy.session", "version": 2, "operations": [{"at": 1, "scroll": {"x": 0, "y": 0, "h": 40000, "v": 0}}]})",
         "$.operations[0].scroll.h:"},
    };
    for (const auto& entry : broken) {
        CAPTURE(entry.text);
        const auto parsed = parseSession(entry.text);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().starts_with(entry.problem));
    }
}
