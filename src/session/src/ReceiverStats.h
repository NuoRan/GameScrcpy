#pragma once

#include "gs/protocol/Codec.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace gs::session::detail {

class ReceiverStats
{
public:
    [[nodiscard]] std::optional<protocol::ReceiverReport> add(const protocol::MediaHeader& header, std::size_t bytes,
                                                              std::int64_t arrivalUs);
    void addLoss(std::uint32_t lostShards, std::uint32_t rebuiltShards);
    [[nodiscard]] std::optional<protocol::ReceiverReport> take(std::int64_t nowUs);
    void setRoundTrip(std::int64_t roundTripUs) noexcept;
    void reset();

private:
    [[nodiscard]] protocol::ReceiverReport report(std::int64_t nowUs) const;
    void restart(std::int64_t nowUs) noexcept;

    std::mutex m_mutex;
    std::uint16_t m_session = 0;
    std::int64_t m_sinceUs = 0;
    std::uint64_t m_bytes = 0;
    std::uint64_t m_frames = 0;
    std::int64_t m_delayMinUs = 0;
    std::int64_t m_delayMaxUs = 0;
    std::int64_t m_delaySumUs = 0;
    std::uint64_t m_lostShards = 0;
    std::uint64_t m_rebuiltShards = 0;
    std::atomic<std::uint32_t> m_roundTripUs{0};
};

}
