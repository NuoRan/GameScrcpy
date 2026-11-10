#pragma once

#include "gs/protocol/Enums.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::session {

using Token = std::array<std::uint8_t, 16>;

enum class SessionPhase : std::uint8_t {
    Idle,
    Deploying,
    Launching,
    Connecting,
    Handshaking,
    Streaming,
    Reconnecting,
    Stopping,
    Stopped,
    Failed,
};

enum class FailureKind : std::uint8_t {
    AdbUnavailable,
    DeviceNotFound,
    DeviceUnauthorized,
    DeviceOffline,
    UnsupportedAndroid,
    DeployFailed,
    LaunchFailed,
    TunnelFailed,
    ConnectFailed,
    HandshakeRejected,
    ProtocolError,
    Timeout,
    ConnectionLost,
    ServerEnded,
    DirectNoAddress,
    DirectUnreachable,
};

struct SessionFailure {
    FailureKind kind = FailureKind::ConnectFailed;
    SessionPhase phase = SessionPhase::Idle;
    protocol::ErrorCode code = protocol::ErrorCode::None;
    std::string detail;
};

struct SessionState {
    SessionPhase phase = SessionPhase::Idle;
    std::optional<SessionFailure> failure;
};

[[nodiscard]] std::string_view phaseName(SessionPhase phase) noexcept;
[[nodiscard]] std::string_view failureName(FailureKind kind) noexcept;

}
