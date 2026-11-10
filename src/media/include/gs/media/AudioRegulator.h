#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gs::media {

struct Compensation {
    int delta = 0;
    int distance = 0;
    bool operator==(const Compensation&) const = default;
};

class AudioRegulator
{
public:
    AudioRegulator(int channels, int sampleRate, std::uint32_t targetFrames);
    AudioRegulator(const AudioRegulator&) = delete;
    AudioRegulator& operator=(const AudioRegulator&) = delete;

    std::optional<Compensation> push(std::span<const float> samples, std::uint32_t inputFrames, std::int64_t ptsUs);

    void pull(std::span<float> out) noexcept;

    [[nodiscard]] std::uint32_t buffered() const noexcept;
    [[nodiscard]] std::uint32_t targetFrames() const noexcept { return m_target; }
    [[nodiscard]] std::uint64_t underflowFrames() const noexcept { return m_underflowTotal.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept { return m_droppedTotal.load(std::memory_order_relaxed); }

private:
    struct Average {
        static constexpr unsigned kRange = 128;
        float value = 0;
        unsigned count = 0;
        void push(float sample) noexcept;
    };

    std::uint32_t write(const float* data, std::uint32_t count) noexcept;
    std::uint32_t read(float* data, std::uint32_t count) noexcept;
    [[nodiscard]] std::uint32_t frames(std::uint32_t milliseconds) const noexcept;
    [[nodiscard]] Compensation compensation(double rate) const noexcept;

    const int m_channels;
    const int m_sampleRate;
    const std::uint32_t m_target;
    const std::uint32_t m_capacity;
    std::vector<float> m_ring;
    std::atomic<std::uint64_t> m_head{0};
    std::atomic<std::uint64_t> m_tail{0};
    std::atomic<bool> m_received{false};
    std::atomic<bool> m_played{false};
    std::atomic<std::uint32_t> m_underflow{0};
    std::atomic<std::uint64_t> m_underflowTotal{0};
    std::atomic<std::uint64_t> m_droppedTotal{0};

    Average m_average;
    std::uint32_t m_sinceResync = 0;
    bool m_compensating = false;
    double m_drift = 0;
    std::int64_t m_nextPtsUs = 0;
};

}
