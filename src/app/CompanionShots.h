#pragma once

#include <gs/protocol/Messages.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gs::app {

enum class ShotFailure : std::uint8_t { Refused, Unsupported, Busy, Error, TimedOut, TooBig, Garbled, Lost };

struct ShotTiming {
    std::chrono::milliseconds first{30000};
    std::chrono::milliseconds between{5000};
};

struct ShotOutcome {
    std::uint32_t requestId = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> picture;
    std::optional<ShotFailure> failure;
    std::string detail;
};

[[nodiscard]] ShotFailure shotFailureOf(protocol::ScreenshotProblem problem) noexcept;

class ShotAssembly
{
public:
    using Clock = std::chrono::steady_clock;

    explicit ShotAssembly(ShotTiming timing = {}) noexcept;

    [[nodiscard]] std::optional<ShotOutcome> begin(std::uint32_t requestId, Clock::time_point now);
    [[nodiscard]] bool waiting() const noexcept { return m_waiting; }
    [[nodiscard]] std::uint32_t requestId() const noexcept { return m_requestId; }
    [[nodiscard]] std::optional<Clock::time_point> deadline() const noexcept;
    [[nodiscard]] std::optional<ShotOutcome> add(const protocol::ScreenshotChunk& chunk, Clock::time_point now);
    [[nodiscard]] std::optional<ShotOutcome> fail(const protocol::ScreenshotFailed& report);
    [[nodiscard]] std::optional<ShotOutcome> expire(Clock::time_point now);
    [[nodiscard]] std::optional<ShotOutcome> abandon(std::string detail);

private:
    [[nodiscard]] ShotOutcome ending(ShotFailure reason, std::string detail);
    void reset();

    ShotTiming m_timing;
    bool m_waiting = false;
    bool m_heard = false;
    std::uint32_t m_requestId = 0;
    Clock::time_point m_since{};
    std::uint16_t m_total = 0;
    std::size_t m_bytes = 0;
    std::size_t m_received = 0;
    std::vector<std::optional<std::vector<std::uint8_t>>> m_chunks;
};

}
