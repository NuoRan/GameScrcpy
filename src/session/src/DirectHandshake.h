#pragma once

#include "gs/platform/UdpSocket.h"
#include "gs/protocol/Messages.h"
#include "gs/session/SessionState.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gs::session::detail {

class DirectHandshake
{
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    static constexpr std::chrono::milliseconds kRetransmitInterval{250};
    static constexpr std::chrono::milliseconds kGiveUpAfter{1500};

    enum class State : std::uint8_t {
        Knocking,
        Proving,
        Accepted,
        Failed,
    };

    DirectHandshake(const Token& token, std::string clientVersion, const platform::UdpEndpoint& server, TimePoint now);

    void receive(std::span<const std::uint8_t> datagram, const platform::UdpEndpoint& from);
    [[nodiscard]] std::span<const std::uint8_t> poll(TimePoint now);
    [[nodiscard]] TimePoint deadline() const noexcept;

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] std::uint32_t connection() const noexcept { return m_connection; }
    [[nodiscard]] const SessionFailure& failure() const noexcept { return m_failure; }

private:
    void prove(const protocol::Nonce& nonce);
    void fail(FailureKind kind, std::string detail, protocol::ErrorCode code = protocol::ErrorCode::None);
    void ignore(std::string why);
    [[nodiscard]] bool finished() const noexcept { return m_state == State::Accepted || m_state == State::Failed; }

    Token m_token;
    std::string m_clientVersion;
    platform::UdpEndpoint m_server;
    TimePoint m_startedAt;
    State m_state = State::Knocking;
    std::vector<std::uint8_t> m_datagram;
    std::optional<TimePoint> m_sentAt;
    protocol::Nonce m_nonce{};
    std::uint32_t m_connection = 0;
    SessionFailure m_failure;
    std::size_t m_ignored = 0;
    std::string m_lastIgnored;
};

}
