#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace gs::session {
struct Heartbeat;
struct VideoStall;
}

namespace gs::app {

class SessionMetrics
{
public:
    void videoFrame(std::uint32_t bytes, std::int64_t ptsUs, std::uint32_t encodeDelayUs, std::int64_t receivedUs,
                    std::int64_t decodeUs) noexcept;
    void decoderHolds(int frames) noexcept;
    void heartbeat(const session::Heartbeat& heartbeat) noexcept;
    void videoStall(const session::VideoStall& stall) noexcept;

    struct Sample {
        double framesPerSecond = 0;
        double megabitsPerSecond = 0;
        std::optional<double> decodeMs;
        std::optional<double> encodeMs;
        std::optional<double> captureToReceiptMs;
        std::optional<double> networkMs;
        std::optional<double> roundTripMs;
        std::optional<double> roundTripBestMs;
        std::optional<double> sendDelayMs;
        std::optional<double> sendDelayMeanMs;
        std::uint64_t stalls = 0;
        double longestStallMs = 0;
        std::uint64_t lostPackets = 0;
        std::optional<double> inputMs;
        std::optional<double> inputBestMs;
        std::optional<double> mergeWaitMeanMs;
        std::optional<double> mergeWaitMaxMs;
        std::optional<double> frameWaitMs;
        int heldFrames = 0;
    };

    [[nodiscard]] Sample sample(std::chrono::microseconds elapsed) noexcept;

    void touchWait(std::int64_t sumUs, std::uint32_t frames, std::int64_t maxUs) noexcept;

    static constexpr std::int64_t kLiveFrameGapUs = 50'000;

private:
    template <std::size_t N>
    struct Window {
        std::array<std::int64_t, N> values{};
        std::size_t next = 0;
        std::size_t count = 0;

        void add(std::int64_t value) noexcept
        {
            values[next] = value;
            next = (next + 1) % N;
            count = count < N ? count + 1 : N;
        }

        [[nodiscard]] std::int64_t at(std::size_t age) const noexcept { return values[(next + N - 1 - age) % N]; }
        [[nodiscard]] std::optional<double> mean() const noexcept;
    };

    struct Beat {
        std::int64_t sentUs = 0;
        std::int64_t roundTripUs = 0;
        std::int64_t oneWayUs = 0;
    };

    static constexpr std::size_t kBeatCapacity = 64;
    static constexpr std::int64_t kBeatWindowUs = 16'000'000;

    void addBeat(std::int64_t sentUs, std::int64_t roundTripUs, std::int64_t oneWayUs) noexcept;
    void waitForFrameAfter(std::int64_t injectedUs) noexcept;
    [[nodiscard]] std::optional<double> halfFrameInterval() const noexcept;

    std::atomic<std::uint64_t> m_frames{0};
    std::atomic<std::uint64_t> m_bytes{0};
    std::atomic<std::int64_t> m_decodeUs{0};
    std::atomic<std::int64_t> m_encodeUs{0};
    std::atomic<std::int64_t> m_latencyUs{0};
    std::atomic<std::int64_t> m_networkUs{0};
    std::atomic<std::uint64_t> m_latencyFrames{0};
    std::atomic<bool> m_clockKnown{false};
    std::atomic<std::int64_t> m_clockOffsetUs{0};
    std::atomic<std::int64_t> m_roundTripUs{0};
    std::atomic<std::int64_t> m_sendDelayUs{0};
    std::atomic<std::int64_t> m_sendDelayMeanUs{0};
    std::atomic<std::int64_t> m_mergeSumUs{0};
    std::atomic<std::int64_t> m_mergeMaxUs{0};
    std::atomic<std::uint32_t> m_mergeFrames{0};
    std::atomic<int> m_heldFrames{0};
    std::atomic<std::uint64_t> m_stalls{0};
    std::atomic<std::int64_t> m_longestStallUs{0};
    std::atomic<std::uint64_t> m_stallLostPackets{0};

    std::mutex m_timingMutex;
    Window<32> m_capturedPts;
    std::array<std::int64_t, 16> m_waiting{};
    std::size_t m_waitingCount = 0;
    std::array<Beat, kBeatCapacity> m_beats{};
    std::size_t m_beatHead = 0;
    std::size_t m_beatCount = 0;
    Window<8> m_frameWait;
};

}
