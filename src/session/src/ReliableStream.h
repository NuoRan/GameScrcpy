#pragma once

#include "gs/protocol/Reliable.h"
#include "gs/session/SessionState.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gs::session::detail {

class ReliableStream
{
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::steady_clock::duration;

    enum class Kind : std::uint8_t {
        Control,
        Bulk,
    };

    static constexpr std::uint32_t kWindow = 32;
    static constexpr std::size_t kPiggyback = 3;
    static constexpr std::chrono::milliseconds kAckDelay{5};
    static constexpr std::chrono::milliseconds kMinRto{20};
    static constexpr std::chrono::milliseconds kInitialRto{100};
    static constexpr std::chrono::milliseconds kMaxRto{1000};

    enum class Verdict : std::uint8_t {
        Accepted,
        Dropped,
        Broken,
    };
    struct Outcome {
        Verdict verdict = Verdict::Accepted;
        std::string_view why;
    };

    struct Stats {
        std::uint64_t datagramsSent = 0;
        std::uint64_t segmentsSent = 0;
        std::uint64_t retransmitted = 0;
        std::uint64_t piggybacked = 0;
        std::uint64_t duplicatesReceived = 0;
    };

    ReliableStream(Kind kind, std::uint32_t connection, const Token& token, std::size_t receiveLimit);

    void send(std::span<const std::uint8_t> bytes);
    [[nodiscard]] Outcome receive(std::span<const std::uint8_t> datagram, TimePoint now);
    [[nodiscard]] std::span<const std::uint8_t> poll(TimePoint now);
    [[nodiscard]] TimePoint deadline() const noexcept;

    [[nodiscard]] std::span<const std::uint8_t> readable() const noexcept;
    void consume(std::size_t count) noexcept;

    [[nodiscard]] bool settled() const noexcept { return m_outgoing.empty(); }
    [[nodiscard]] bool acknowledgementPending() const noexcept { return m_ackDue.has_value(); }
    [[nodiscard]] std::uint32_t inFlight() const noexcept { return m_sentEnd - m_base; }
    [[nodiscard]] const Stats& stats() const noexcept { return m_stats; }
    [[nodiscard]] Duration rto() const noexcept;
    [[nodiscard]] std::optional<Duration> smoothedRtt() const noexcept { return m_srtt; }

private:
    struct Outgoing {
        std::uint32_t seq = 0;
        std::vector<std::uint8_t> bytes;
        TimePoint firstSent{};
        TimePoint lastSent{};
        std::uint64_t lastTransmission = 0;
        bool acked = false;
        bool lost = false;
        bool resent = false;
    };

    [[nodiscard]] std::size_t sentCount() const noexcept { return m_sentEnd - m_base; }
    [[nodiscard]] const Outgoing* oldestUnacked() const noexcept;
    [[nodiscard]] bool sendable() const noexcept;
    [[nodiscard]] bool acceptCounter(std::uint64_t counter) noexcept;
    [[nodiscard]] Outcome acknowledge(const protocol::ReliableBlock& block, TimePoint now);
    void take(const protocol::ReliableBlock& block, TimePoint now);
    void sample(Duration rtt) noexcept;
    [[nodiscard]] std::uint32_t sackBits() const noexcept;

    Kind m_kind;
    std::uint32_t m_connection;
    Token m_token;
    std::size_t m_receiveLimit;
    std::size_t m_maxSegment;

    std::deque<Outgoing> m_outgoing;
    std::uint32_t m_base = 0;
    std::uint32_t m_sentEnd = 0;
    std::uint32_t m_nextSeq = 0;
    std::uint64_t m_transmissions = 0;
    std::uint64_t m_counter = 0;
    unsigned m_backoff = 0;
    std::optional<Duration> m_srtt;
    Duration m_rttvar{};

    std::uint32_t m_ack = 0;
    std::array<std::optional<std::vector<std::uint8_t>>, kWindow + 1> m_held;
    std::vector<std::uint8_t> m_readable;
    std::size_t m_readOffset = 0;
    std::optional<TimePoint> m_ackDue;
    std::uint64_t m_highestCounter = 0;
    std::uint64_t m_seenCounters = 0;

    std::array<std::uint8_t, protocol::kReliableDatagramLimit> m_datagram{};
    Stats m_stats;
};

}
