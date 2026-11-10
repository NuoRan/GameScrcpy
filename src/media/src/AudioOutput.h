#pragma once

#include "gs/media/AudioRegulator.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace gs::media {

class AudioOutput
{
public:
    AudioOutput(AudioRegulator& source, int channels, int sampleRate, const std::atomic<float>& gain);
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    [[nodiscard]] std::uint32_t periodFrames() const noexcept { return m_period.load(std::memory_order_relaxed); }
    [[nodiscard]] bool lowLatency() const noexcept { return m_lowLatency.load(std::memory_order_relaxed); }

private:
    struct Wake;

    void run(const std::stop_token& stop);

    AudioRegulator& m_source;
    const int m_channels;
    const int m_sampleRate;
    const std::atomic<float>& m_gain;
    std::unique_ptr<Wake> m_wake;
    std::atomic<std::uint32_t> m_period{0};
    std::atomic<bool> m_lowLatency{false};
    std::jthread m_thread;
};

}
