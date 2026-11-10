#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace gs::capture {

class AudioCapture
{
public:
    using Samples = std::function<void(std::span<const float> interleaved, int channels, int sampleRate,
                                       std::int64_t ptsUs)>;

    AudioCapture(std::wstring endpoint, Samples samples);
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    [[nodiscard]] std::string problem() const;

private:
    struct Wake;

    void run(const std::stop_token& stop);
    void fail(std::string reason);

    const std::wstring m_endpoint;
    Samples m_samples;
    std::unique_ptr<Wake> m_wake;
    mutable std::mutex m_mutex;
    std::string m_problem;
    std::jthread m_thread;
};

}
