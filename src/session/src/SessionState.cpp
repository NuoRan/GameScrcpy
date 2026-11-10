#include "gs/session/SessionState.h"

namespace gs::session {

std::string_view phaseName(SessionPhase phase) noexcept
{
    switch (phase) {
    case SessionPhase::Idle:
        return "idle";
    case SessionPhase::Deploying:
        return "deploying";
    case SessionPhase::Launching:
        return "launching";
    case SessionPhase::Connecting:
        return "connecting";
    case SessionPhase::Handshaking:
        return "handshaking";
    case SessionPhase::Streaming:
        return "streaming";
    case SessionPhase::Reconnecting:
        return "reconnecting";
    case SessionPhase::Stopping:
        return "stopping";
    case SessionPhase::Stopped:
        return "stopped";
    case SessionPhase::Failed:
        return "failed";
    }
    return "unknown";
}

std::string_view failureName(FailureKind kind) noexcept
{
    switch (kind) {
    case FailureKind::AdbUnavailable:
        return "adb unavailable";
    case FailureKind::DeviceNotFound:
        return "device not found";
    case FailureKind::DeviceUnauthorized:
        return "device unauthorized";
    case FailureKind::DeviceOffline:
        return "device offline";
    case FailureKind::UnsupportedAndroid:
        return "unsupported Android version";
    case FailureKind::DeployFailed:
        return "deploy failed";
    case FailureKind::LaunchFailed:
        return "launch failed";
    case FailureKind::TunnelFailed:
        return "tunnel failed";
    case FailureKind::ConnectFailed:
        return "connect failed";
    case FailureKind::HandshakeRejected:
        return "handshake rejected";
    case FailureKind::ProtocolError:
        return "protocol error";
    case FailureKind::Timeout:
        return "timeout";
    case FailureKind::ConnectionLost:
        return "connection lost";
    case FailureKind::ServerEnded:
        return "server ended";
    case FailureKind::DirectNoAddress:
        return "no address for WiFi direct";
    case FailureKind::DirectUnreachable:
        return "WiFi direct unreachable";
    }
    return "unknown";
}

}
