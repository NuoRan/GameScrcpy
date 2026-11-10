#pragma once

#include "gs/base/Expected.h"
#include "gs/protocol/Codec.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gs::media {

struct AudioStats {
    std::uint32_t bufferedFrames = 0;
    std::uint32_t targetFrames = 0;
    std::uint64_t underflowFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint32_t periodFrames = 0;
    bool lowLatency = false;
    int sampleRate = 0;
};

class AudioPlayer
{
public:
    explicit AudioPlayer(std::chrono::milliseconds target = std::chrono::milliseconds(50));
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    [[nodiscard]] Expected<void, std::string> push(const protocol::MediaPacket& packet);
    [[nodiscard]] Expected<void, std::string> pushPcm(std::span<const float> interleaved, int channels,
                                                      int sampleRate, std::int64_t ptsUs);
    void close();

    void setVolume(float volume) noexcept;
    void setMuted(bool muted) noexcept;
    [[nodiscard]] AudioStats stats() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
