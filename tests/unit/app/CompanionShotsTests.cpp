#include "CompanionLink.h"
#include "CompanionShots.h"

#include <gs/protocol/Codec.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace std::chrono_literals;
using gs::app::CompanionChannel;
using gs::app::CompanionEvent;
using gs::app::CompanionLink;
using gs::app::CompanionNotice;
using gs::app::CompanionRead;
using gs::app::CompanionTiming;
using gs::app::ShotAssembly;
using gs::app::ShotFailure;
using gs::app::ShotOutcome;
using gs::app::ShotTiming;
namespace protocol = gs::protocol;

namespace {

using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;

Bytes picture(std::size_t size, std::uint8_t seed = 3)
{
    Bytes bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(i * 13 + seed);
    }
    return bytes;
}

Bytes part(const Bytes& whole, std::size_t index, std::size_t size)
{
    const std::size_t start = index * protocol::kMaxScreenshotChunk;
    const std::size_t end = std::min(whole.size(), start + size);
    return Bytes(whole.begin() + static_cast<std::ptrdiff_t>(start), whole.begin() + static_cast<std::ptrdiff_t>(end));
}

protocol::ScreenshotChunk chunkOf(std::uint32_t requestId, std::uint16_t index, std::uint16_t total, Bytes data)
{
    return protocol::ScreenshotChunk{.requestId = requestId, .index = index, .total = total, .data = std::move(data)};
}

template <typename M>
Bytes frameOf(const M& message)
{
    const auto frame = protocol::encodeFrame(message);
    return frame.has_value() ? *frame : Bytes{};
}

class Notices
{
public:
    void add(const CompanionNotice& notice)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_seen.push_back(notice);
        }
        m_changed.notify_all();
    }

    std::optional<CompanionNotice> waitFor(CompanionEvent event, std::size_t nth = 1,
                                           std::chrono::milliseconds timeout = 10s)
    {
        std::unique_lock lock(m_mutex);
        std::optional<CompanionNotice> found;
        m_changed.wait_for(lock, timeout, [&] {
            std::size_t count = 0;
            for (const CompanionNotice& notice : m_seen) {
                if (notice.event == event && ++count == nth) {
                    found = notice;
                    return true;
                }
            }
            return false;
        });
        return found;
    }

    [[nodiscard]] bool saw(CompanionEvent event) const
    {
        const std::lock_guard lock(m_mutex);
        return std::ranges::any_of(m_seen, [event](const CompanionNotice& notice) { return notice.event == event; });
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<CompanionNotice> m_seen;
};

class MemoryChannel final : public CompanionChannel
{
public:
    using Answer = std::function<std::vector<Bytes>(const protocol::ToServerMessage& message)>;

    explicit MemoryChannel(Answer answer) : m_answer(std::move(answer)) {}

    void setTimeouts(std::chrono::milliseconds read, std::chrono::milliseconds) override
    {
        const std::lock_guard lock(m_mutex);
        m_read = read;
    }

    bool send(std::span<const std::uint8_t> frame) override
    {
        const auto message = protocol::decodeToServerFrame(protocol::FrameContext::Companion, frame);
        if (!message) {
            return false;
        }
        std::vector<Bytes> replies;
        if (std::holds_alternative<protocol::CompanionHello>(*message)) {
            replies.push_back(frameOf(protocol::CompanionWelcome{.width = 1080, .height = 2400}));
        } else if (!std::holds_alternative<protocol::Goodbye>(*message)) {
            replies = m_answer(*message);
        }
        {
            const std::lock_guard lock(m_mutex);
            m_sent.push_back(*message);
            m_closed = m_closed || std::holds_alternative<protocol::Goodbye>(*message);
            for (Bytes& reply : replies) {
                m_inbox.push_back(std::move(reply));
            }
        }
        m_changed.notify_all();
        return true;
    }

    CompanionRead receive(std::vector<std::uint8_t>& frame) override
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, m_read, [this] { return !m_inbox.empty() || m_closed || m_interrupted; })) {
            return CompanionRead::TimedOut;
        }
        if (m_inbox.empty()) {
            return CompanionRead::Closed;
        }
        frame = std::move(m_inbox.front());
        m_inbox.pop_front();
        return CompanionRead::Frame;
    }

    void interrupt() override
    {
        {
            const std::lock_guard lock(m_mutex);
            m_interrupted = true;
        }
        m_changed.notify_all();
    }

    template <typename M>
    [[nodiscard]] std::vector<M> sentOf() const
    {
        const std::lock_guard lock(m_mutex);
        std::vector<M> found;
        for (const protocol::ToServerMessage& message : m_sent) {
            if (const auto* typed = std::get_if<M>(&message)) {
                found.push_back(*typed);
            }
        }
        return found;
    }

private:
    Answer m_answer;
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::chrono::milliseconds m_read{5000};
    std::deque<Bytes> m_inbox;
    std::vector<protocol::ToServerMessage> m_sent;
    bool m_closed = false;
    bool m_interrupted = false;
};

}

TEST_CASE("screenshot assembly: chunks in any order make the picture, others are ignored (ADR-0105)")
{
    const Clock::time_point start = Clock::now();
    const Bytes whole = picture(2 * protocol::kMaxScreenshotChunk + 100);
    ShotAssembly shots;
    CHECK_FALSE(shots.waiting());
    CHECK_FALSE(shots.deadline().has_value());
    CHECK_FALSE(shots.add(chunkOf(1, 0, 3, part(whole, 0, protocol::kMaxScreenshotChunk)), start).has_value());

    CHECK_FALSE(shots.begin(7, start).has_value());
    CHECK(shots.waiting());
    CHECK(shots.requestId() == 7);
    CHECK(shots.deadline() == start + 30s);
    CHECK_FALSE(shots.add(chunkOf(8, 0, 1, Bytes{1, 2, 3}), start).has_value());
    CHECK_FALSE(shots.fail(protocol::ScreenshotFailed{.requestId = 8}).has_value());
    CHECK_FALSE(shots.add(chunkOf(7, 2, 3, part(whole, 2, protocol::kMaxScreenshotChunk)), start + 1s).has_value());
    CHECK(shots.deadline() == start + 1s + 5s);
    CHECK_FALSE(shots.add(chunkOf(7, 0, 3, part(whole, 0, protocol::kMaxScreenshotChunk)), start + 2s).has_value());
    const std::optional<ShotOutcome> done =
        shots.add(chunkOf(7, 1, 3, part(whole, 1, protocol::kMaxScreenshotChunk)), start + 3s);
    REQUIRE(done.has_value());
    CHECK(done->requestId == 7);
    CHECK_FALSE(done->failure.has_value());
    REQUIRE(done->picture != nullptr);
    CHECK(*done->picture == whole);
    CHECK_FALSE(shots.waiting());

    CHECK_FALSE(shots.begin(9, start).has_value());
    const std::optional<ShotOutcome> single = shots.add(chunkOf(9, 0, 1, Bytes{0xFF, 0xD8, 0xFF, 0xD9}), start);
    REQUIRE(single.has_value());
    REQUIRE(single->picture != nullptr);
    CHECK(*single->picture == Bytes{0xFF, 0xD8, 0xFF, 0xD9});
}

TEST_CASE("screenshot assembly: a missing chunk, a silent companion and a garbled answer fail the screenshot (ADR-0105)")
{
    const Clock::time_point start = Clock::now();
    ShotAssembly shots(ShotTiming{.first = 30s, .between = 5s});
    CHECK_FALSE(shots.begin(1, start).has_value());
    CHECK_FALSE(shots.expire(start + 29s).has_value());
    const std::optional<ShotOutcome> silent = shots.expire(start + 30s);
    REQUIRE(silent.has_value());
    CHECK(silent->requestId == 1);
    CHECK(silent->failure == ShotFailure::TimedOut);
    CHECK(silent->detail == "the companion did not answer in time");
    CHECK(silent->picture == nullptr);
    CHECK_FALSE(shots.waiting());

    CHECK_FALSE(shots.begin(2, start).has_value());
    CHECK_FALSE(shots.add(chunkOf(2, 0, 2, picture(protocol::kMaxScreenshotChunk)), start + 10s).has_value());
    CHECK_FALSE(shots.expire(start + 14s).has_value());
    const std::optional<ShotOutcome> missing = shots.expire(start + 15s);
    REQUIRE(missing.has_value());
    CHECK(missing->failure == ShotFailure::TimedOut);
    CHECK(missing->detail == "the next chunk of the screenshot did not come in time");

    CHECK_FALSE(shots.begin(3, start).has_value());
    CHECK_FALSE(shots.add(chunkOf(3, 0, 2, Bytes(10, 1)), start).has_value());
    const std::optional<ShotOutcome> twice = shots.add(chunkOf(3, 0, 2, Bytes(10, 1)), start);
    REQUIRE(twice.has_value());
    CHECK(twice->failure == ShotFailure::Garbled);

    CHECK_FALSE(shots.begin(4, start).has_value());
    CHECK_FALSE(shots.add(chunkOf(4, 0, 2, Bytes(10, 1)), start).has_value());
    const std::optional<ShotOutcome> recount = shots.add(chunkOf(4, 1, 3, Bytes(10, 1)), start);
    REQUIRE(recount.has_value());
    CHECK(recount->failure == ShotFailure::Garbled);

    CHECK_FALSE(shots.begin(5, start).has_value());
    const std::optional<ShotOutcome> replaced = shots.begin(6, start);
    REQUIRE(replaced.has_value());
    CHECK(replaced->requestId == 5);
    CHECK(replaced->failure == ShotFailure::Lost);
    CHECK(shots.requestId() == 6);
    const std::optional<ShotOutcome> abandoned = shots.abandon("the connection to the companion ended");
    REQUIRE(abandoned.has_value());
    CHECK(abandoned->requestId == 6);
    CHECK(abandoned->failure == ShotFailure::Lost);
    CHECK_FALSE(shots.abandon("again").has_value());
}

TEST_CASE("screenshot assembly: more than 4 MiB is refused (ADR-0105)")
{
    const Clock::time_point start = Clock::now();
    ShotAssembly shots;
    CHECK_FALSE(shots.begin(1, start).has_value());
    const Bytes full(protocol::kMaxScreenshotChunk, 0x5A);
    std::optional<ShotOutcome> outcome;
    std::uint16_t sent = 0;
    while (!outcome && sent < protocol::kMaxScreenshotChunks) {
        outcome = shots.add(chunkOf(1, sent, protocol::kMaxScreenshotChunks, full), start);
        ++sent;
    }
    REQUIRE(outcome.has_value());
    CHECK(sent == protocol::kMaxScreenshotChunks);
    CHECK(outcome->failure == ShotFailure::TooBig);
    CHECK(outcome->picture == nullptr);
}

TEST_CASE("screenshot assembly: the companion's reasons are kept (ADR-0105)")
{
    const Clock::time_point start = Clock::now();
    const std::vector<std::pair<protocol::ScreenshotProblem, ShotFailure>> reasons{
        {protocol::ScreenshotProblem::Refused, ShotFailure::Refused},
        {protocol::ScreenshotProblem::Unsupported, ShotFailure::Unsupported},
        {protocol::ScreenshotProblem::Busy, ShotFailure::Busy},
        {protocol::ScreenshotProblem::Error, ShotFailure::Error}};
    for (const auto& [problem, failure] : reasons) {
        ShotAssembly shots;
        CHECK_FALSE(shots.begin(11, start).has_value());
        const std::optional<ShotOutcome> outcome =
            shots.fail(protocol::ScreenshotFailed{.requestId = 11, .reason = problem, .message = "why"});
        REQUIRE(outcome.has_value());
        CHECK(outcome->failure == failure);
        CHECK(outcome->detail == "why");
        CHECK(gs::app::shotFailureOf(problem) == failure);
    }
}

TEST_CASE("companion link: a screenshot is asked for, put together, refused, or given up on when a chunk is missing")
{
    const Bytes whole = picture(2 * protocol::kMaxScreenshotChunk + 4321, 9);
    auto owned = std::make_unique<MemoryChannel>([&whole](const protocol::ToServerMessage& message) -> std::vector<Bytes> {
        const auto* request = std::get_if<protocol::ScreenshotRequest>(&message);
        if (request == nullptr) {
            return {};
        }
        switch (request->requestId) {
        case 1:
            return {frameOf(chunkOf(1, 2, 3, part(whole, 2, protocol::kMaxScreenshotChunk))),
                    frameOf(chunkOf(1, 0, 3, part(whole, 0, protocol::kMaxScreenshotChunk))),
                    frameOf(chunkOf(77, 0, 1, Bytes{1})),
                    frameOf(chunkOf(1, 1, 3, part(whole, 1, protocol::kMaxScreenshotChunk)))};
        case 2:
            return {frameOf(protocol::ScreenshotFailed{.requestId = 2,
                                                       .reason = protocol::ScreenshotProblem::Refused,
                                                       .message = "the screen capture was not allowed on the phone"})};
        case 3:
            return {frameOf(chunkOf(3, 0, 2, part(whole, 0, protocol::kMaxScreenshotChunk)))};
        default:
            return {};
        }
    });
    MemoryChannel* channel = owned.get();
    Notices notices;
    CompanionTiming timing;
    timing.ping = 60s;
    timing.silence = 30s;
    timing.drain = 2s;
    timing.shotFirst = 5s;
    timing.shotBetween = 300ms;
    CompanionLink link(std::move(owned), protocol::CompanionWelcome{}, "esp32-COM5",
                       [&notices](const CompanionNotice& notice) { notices.add(notice); }, timing);
    REQUIRE(notices.waitFor(CompanionEvent::Connected).has_value());

    link.requestScreenshot(1, 0, 90);
    const std::optional<CompanionNotice> shot = notices.waitFor(CompanionEvent::Screenshot);
    REQUIRE(shot.has_value());
    CHECK(shot->shot == 1);
    REQUIRE(shot->picture != nullptr);
    CHECK(*shot->picture == whole);
    CHECK_FALSE(shot->shotFailure.has_value());
    CHECK_FALSE(gs::app::isFinal(CompanionEvent::Screenshot));
    CHECK_FALSE(gs::app::isFinal(CompanionEvent::ScreenshotFailed));

    link.requestScreenshot(2, 1280, 0);
    const std::optional<CompanionNotice> refused = notices.waitFor(CompanionEvent::ScreenshotFailed);
    REQUIRE(refused.has_value());
    CHECK(refused->shot == 2);
    CHECK(refused->shotFailure == ShotFailure::Refused);
    CHECK(refused->detail == "the screen capture was not allowed on the phone");

    link.requestScreenshot(3, 0, 90);
    const std::optional<CompanionNotice> missing = notices.waitFor(CompanionEvent::ScreenshotFailed, 2);
    REQUIRE(missing.has_value());
    CHECK(missing->shot == 3);
    CHECK(missing->shotFailure == ShotFailure::TimedOut);
    CHECK_FALSE(notices.saw(CompanionEvent::Lost));

    const std::vector<protocol::ScreenshotRequest> requests = channel->sentOf<protocol::ScreenshotRequest>();
    REQUIRE(requests.size() == 3);
    CHECK(requests[0] == protocol::ScreenshotRequest{.requestId = 1, .maxSide = 0, .quality = 90});
    CHECK(requests[1] == protocol::ScreenshotRequest{.requestId = 2, .maxSide = 1280, .quality = 1});
    CHECK(requests[2].requestId == 3);

    link.stop();
    REQUIRE(notices.waitFor(CompanionEvent::Ended).has_value());
    link.requestScreenshot(4, 0, 90);
    const std::optional<CompanionNotice> gone = notices.waitFor(CompanionEvent::ScreenshotFailed, 3, 2s);
    REQUIRE(gone.has_value());
    CHECK(gone->shot == 4);
    CHECK(gone->shotFailure == ShotFailure::Lost);
}
