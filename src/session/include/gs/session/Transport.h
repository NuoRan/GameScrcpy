#pragma once

#include "gs/base/Expected.h"
#include "gs/protocol/Codec.h"
#include "gs/session/SessionState.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace gs::session {

enum class ChannelError : std::uint8_t { Closed, TimedOut, Malformed };

struct ChannelFailure {
    ChannelError error = ChannelError::Closed;
    protocol::DecodeError decode = protocol::DecodeError::Truncated;
};

class FrameChannel
{
public:
    virtual ~FrameChannel() = default;

    [[nodiscard]] virtual Expected<std::span<const std::uint8_t>, ChannelFailure> receive() = 0;
    [[nodiscard]] virtual bool send(std::span<const std::uint8_t> frames) = 0;
    virtual void setSendTimeout(std::chrono::milliseconds timeout) = 0;
    virtual void drain(std::chrono::steady_clock::time_point deadline) = 0;
    virtual void interrupt() noexcept = 0;
};

class MediaChannel
{
public:
    struct Packet {
        std::span<const std::uint8_t> bytes;
        std::uint32_t lostBefore = 0;
    };

    struct LossCounts {
        std::uint32_t lost = 0;
        std::uint32_t rebuilt = 0;
    };

    virtual ~MediaChannel() = default;

    [[nodiscard]] virtual Expected<Packet, ChannelFailure> receive() = 0;
    virtual void interrupt() noexcept = 0;
    [[nodiscard]] virtual LossCounts takeLossCounts() noexcept { return {}; }
};

enum class TransportKind : std::uint8_t {
    AdbTunnel,
    WifiDirect,
};

class Link
{
public:
    virtual ~Link() = default;

    [[nodiscard]] virtual TransportKind kind() const noexcept = 0;
    [[nodiscard]] virtual const protocol::Welcome& welcome() const = 0;
    [[nodiscard]] virtual FrameChannel& control() = 0;
    [[nodiscard]] virtual Expected<std::unique_ptr<MediaChannel>, SessionFailure> openMedia(
        protocol::MediaChannel channel, std::stop_token stop) = 0;
    [[nodiscard]] virtual Expected<std::unique_ptr<FrameChannel>, SessionFailure> openBulk(std::stop_token stop);
};

inline Expected<std::unique_ptr<FrameChannel>, SessionFailure> Link::openBulk(std::stop_token)
{
    return makeUnexpected(SessionFailure{.kind = FailureKind::HandshakeRejected,
                                         .code = protocol::ErrorCode::ChannelUnavailable,
                                         .detail = "this connection has no bulk channel"});
}

class Transport
{
public:
    virtual ~Transport() = default;

    [[nodiscard]] virtual Expected<std::unique_ptr<Link>, SessionFailure> establish(const Token& token,
                                                                                    const std::string& clientVersion,
                                                                                    std::stop_token stop) = 0;
};

}
