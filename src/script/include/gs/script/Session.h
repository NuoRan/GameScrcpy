#pragma once

#include "gs/script/Services.h"

#include <gs/base/Clock.h>
#include <gs/base/Expected.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gs::script {

enum class SessionEventKind : std::uint8_t {
    Press,
    Release,
    State,
    Run,
    Answer,
};

struct SessionEvent {
    Microseconds at{0};
    SessionEventKind kind = SessionEventKind::Run;
    std::string id;
    bool value = false;

    friend bool operator==(const SessionEvent&, const SessionEvent&) = default;
};

enum class SessionTouchAction : std::uint8_t {
    Down,
    Up,
    Move,
    Cancel,
};

struct SessionTouchPoint {
    int pointer = 0;
    SessionTouchAction action = SessionTouchAction::Move;
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const SessionTouchPoint&, const SessionTouchPoint&) = default;
};

struct SessionTouch {
    std::vector<SessionTouchPoint> points;

    friend bool operator==(const SessionTouch&, const SessionTouch&) = default;
};

struct SessionKey {
    bool down = true;
    std::uint32_t keycode = 0;
    std::uint32_t metaState = 0;
    std::uint32_t repeat = 0;

    friend bool operator==(const SessionKey&, const SessionKey&) = default;
};

struct SessionScroll {
    double x = 0.0;
    double y = 0.0;
    int horizontal = 0;
    int vertical = 0;

    friend bool operator==(const SessionScroll&, const SessionScroll&) = default;
};

struct SessionText {
    std::size_t length = 0;

    friend bool operator==(const SessionText&, const SessionText&) = default;
};

using SessionAction = std::variant<SessionTouch, SessionKey, SessionScroll, SessionText>;

struct SessionOperation {
    Microseconds at{0};
    SessionAction action;

    friend bool operator==(const SessionOperation&, const SessionOperation&) = default;
};

struct Session {
    DeviceInfo device;
    std::int64_t startedAtMs = 0;
    double frameRate = 60.0;
    std::string profile;
    std::vector<SessionEvent> events;
    std::string video;
    std::string audio;
    std::int64_t videoStartPtsUs = 0;
    std::int64_t durationMs = 0;
    std::vector<SessionOperation> operations;
};

inline constexpr std::string_view kSessionFormat = "gamescrcpy.session";
inline constexpr int kSessionVersion = 2;
inline constexpr std::int64_t kDefaultSessionStartMs = 1'767'225'600'000;
inline constexpr std::size_t kMostSessionEvents = 100'000;
inline constexpr std::size_t kMostSessionOperations = 1'000'000;
inline constexpr std::int64_t kLongestSessionUs = 24LL * 3600 * 1'000'000;

[[nodiscard]] Expected<Session, std::string> parseSession(std::string_view text);
[[nodiscard]] Expected<Session, std::string> loadSession(const std::filesystem::path& file);
[[nodiscard]] std::string writeSession(const Session& session);

}
