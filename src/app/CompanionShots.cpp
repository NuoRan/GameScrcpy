#include "CompanionShots.h"

#include <gs/protocol/Constants.h>

#include <utility>

namespace gs::app {

ShotFailure shotFailureOf(protocol::ScreenshotProblem problem) noexcept
{
    switch (problem) {
    case protocol::ScreenshotProblem::Refused:
        return ShotFailure::Refused;
    case protocol::ScreenshotProblem::Unsupported:
        return ShotFailure::Unsupported;
    case protocol::ScreenshotProblem::Busy:
        return ShotFailure::Busy;
    case protocol::ScreenshotProblem::Error:
        break;
    }
    return ShotFailure::Error;
}

ShotAssembly::ShotAssembly(ShotTiming timing) noexcept
    : m_timing(timing)
{
}

std::optional<ShotOutcome> ShotAssembly::begin(std::uint32_t requestId, Clock::time_point now)
{
    std::optional<ShotOutcome> replaced;
    if (m_waiting) {
        replaced = ending(ShotFailure::Lost, "another screenshot was asked for");
    }
    m_waiting = true;
    m_requestId = requestId;
    m_since = now;
    return replaced;
}

std::optional<ShotAssembly::Clock::time_point> ShotAssembly::deadline() const noexcept
{
    if (!m_waiting) {
        return std::nullopt;
    }
    return m_since + (m_heard ? m_timing.between : m_timing.first);
}

std::optional<ShotOutcome> ShotAssembly::add(const protocol::ScreenshotChunk& chunk, Clock::time_point now)
{
    if (!m_waiting || chunk.requestId != m_requestId) {
        return std::nullopt;
    }
    if (chunk.total == 0 || chunk.total > protocol::kMaxScreenshotChunks || chunk.index >= chunk.total) {
        return ending(ShotFailure::Garbled, "a chunk of the screenshot is out of range");
    }
    if (!m_heard) {
        m_heard = true;
        m_total = chunk.total;
        m_chunks.assign(chunk.total, std::nullopt);
    } else if (chunk.total != m_total) {
        return ending(ShotFailure::Garbled, "the chunks of the screenshot disagree on how many there are");
    }
    std::optional<std::vector<std::uint8_t>>& slot = m_chunks[chunk.index];
    if (slot) {
        return ending(ShotFailure::Garbled, "a chunk of the screenshot came twice");
    }
    m_bytes += chunk.data.size();
    if (m_bytes > protocol::kMaxScreenshotBytes) {
        return ending(ShotFailure::TooBig, "the screenshot is larger than 4 MiB");
    }
    slot = chunk.data;
    ++m_received;
    m_since = now;
    if (m_received < m_total) {
        return std::nullopt;
    }
    auto picture = std::make_shared<std::vector<std::uint8_t>>();
    picture->reserve(m_bytes);
    for (const std::optional<std::vector<std::uint8_t>>& part : m_chunks) {
        picture->insert(picture->end(), part->begin(), part->end());
    }
    ShotOutcome done{.requestId = m_requestId, .picture = std::move(picture)};
    reset();
    return done;
}

std::optional<ShotOutcome> ShotAssembly::fail(const protocol::ScreenshotFailed& report)
{
    if (!m_waiting || report.requestId != m_requestId) {
        return std::nullopt;
    }
    return ending(shotFailureOf(report.reason), report.message);
}

std::optional<ShotOutcome> ShotAssembly::expire(Clock::time_point now)
{
    const std::optional<Clock::time_point> due = deadline();
    if (!due || now < *due) {
        return std::nullopt;
    }
    return ending(ShotFailure::TimedOut,
                  m_heard ? "the next chunk of the screenshot did not come in time" : "the companion did not answer in time");
}

std::optional<ShotOutcome> ShotAssembly::abandon(std::string detail)
{
    if (!m_waiting) {
        return std::nullopt;
    }
    return ending(ShotFailure::Lost, std::move(detail));
}

ShotOutcome ShotAssembly::ending(ShotFailure reason, std::string detail)
{
    ShotOutcome outcome{.requestId = m_requestId, .failure = reason, .detail = std::move(detail)};
    reset();
    return outcome;
}

void ShotAssembly::reset()
{
    m_waiting = false;
    m_heard = false;
    m_total = 0;
    m_bytes = 0;
    m_received = 0;
    m_chunks = {};
}

}
