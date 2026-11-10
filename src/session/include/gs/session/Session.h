#pragma once

#include "gs/protocol/Codec.h"
#include "gs/session/ServerHost.h"
#include "gs/session/SessionState.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace gs::session {

struct SessionOptions {
    std::string clientVersion;
    std::chrono::milliseconds linger{15000};
    std::string serverLogLevel = "info";
    std::optional<protocol::ConfigureVideo> video;
    std::optional<protocol::ConfigureAudio> audio;
    bool bulk = false;

    std::chrono::milliseconds handshakeTimeout{5000};
    std::chrono::milliseconds pingInterval{1000};
    std::chrono::milliseconds reportInterval{200};
    std::chrono::milliseconds pongTimeout{6000};
    std::chrono::milliseconds stopGrace{2000};
};

struct Heartbeat {
    std::chrono::microseconds roundTrip{};
    std::chrono::microseconds clockOffset{};
    std::chrono::microseconds sendDelay{};
    std::int64_t sentUs = 0;
    std::int64_t serverTimeUs = 0;
};

template <typename M>
inline constexpr bool kIsInput = std::is_same_v<M, protocol::TouchFrame> || std::is_same_v<M, protocol::Key> ||
                                 std::is_same_v<M, protocol::Text> || std::is_same_v<M, protocol::Scroll> ||
                                 std::is_same_v<M, protocol::BackOrScreenOn> ||
                                 std::is_same_v<M, protocol::UhidInput>;

class SessionListener
{
public:
    virtual ~SessionListener() = default;

    virtual void onStateChanged(const SessionState&) {}
    virtual void onTransport(TransportKind) {}
    virtual void onWelcome(const protocol::Welcome&) {}
    virtual void onMessage(const protocol::ToClientMessage&) {}
    virtual void onHeartbeat(const Heartbeat&) {}
    virtual void onVideoOpened() {}
    virtual void onVideoClosed() {}
    virtual void onVideoPacket(const protocol::MediaPacket&) {}
    virtual void onAudioOpened() {}
    virtual void onAudioClosed() {}
    virtual void onAudioPacket(const protocol::MediaPacket&) {}
    virtual void onBulkOpened() {}
    virtual void onBulkClosed() {}
};

class Session
{
public:
    Session(std::unique_ptr<ServerHost> host, SessionOptions options, SessionListener& listener);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void start();
    void stop() noexcept;

    [[nodiscard]] SessionPhase phase() const noexcept;

    template <typename M>
    bool send(const M& message)
    {
        constexpr auto type = static_cast<std::uint16_t>(M::kType);
        static_assert(protocol::directionOf(type) == protocol::Direction::ToServer &&
                          protocol::contextOf(type) == protocol::FrameContext::Control,
                      "only control messages travel to the server");
        if constexpr (std::is_same_v<M, protocol::ConfigureVideo> || std::is_same_v<M, protocol::SetVideoBitrate> ||
                      std::is_same_v<M, protocol::SetMaxFps> || std::is_same_v<M, protocol::SetVideoPaused>) {
            rememberVideo(message);
        }
        std::array<std::uint8_t, 512> buffer;
        if (const auto written = protocol::encodeFrameInto(message, buffer)) {
            return sendFrame(std::span<const std::uint8_t>(buffer).first(*written), kIsInput<M>);
        }
        const auto frame = protocol::encodeFrame(message);
        return frame && sendFrame(*frame, kIsInput<M>);
    }

private:
    class Impl;

    bool sendFrame(std::span<const std::uint8_t> frame, bool input);
    void rememberVideo(const protocol::ConfigureVideo& message);
    void rememberVideo(const protocol::SetVideoBitrate& message);
    void rememberVideo(const protocol::SetMaxFps& message);
    void rememberVideo(const protocol::SetVideoPaused& message);

    std::unique_ptr<Impl> m_impl;
};

}
