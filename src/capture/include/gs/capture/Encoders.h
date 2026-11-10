#pragma once

#include "gs/capture/CropDetector.h"

#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Codec.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct ID3D11Device;

namespace gs::capture {

[[nodiscard]] std::vector<std::uint8_t> h264ParameterSets(std::span<const std::uint8_t> accessUnit);
[[nodiscard]] std::vector<std::uint8_t> aacConfig(int sampleRate, int channels);

class VideoEncoder
{
public:
    using Output = std::function<void(const protocol::MediaPacket& packet)>;

    VideoEncoder(ID3D11Device* device, Output output);
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    void encode(const media::VideoFrame& frame, const PixelBox& region, std::int64_t atUs);
    [[nodiscard]] std::string problem() const;
    [[nodiscard]] bool hardware() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

struct EncodedSound {
    bool config = false;
    std::uint32_t sampleRate = 0;
    std::uint8_t channels = 0;
    std::int64_t ptsUs = 0;
    std::span<const std::uint8_t> bytes;
};

class AudioEncoder
{
public:
    using Output = std::function<void(const EncodedSound& sound)>;

    explicit AudioEncoder(Output output);
    ~AudioEncoder();
    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    void encode(std::span<const float> interleaved, int channels, int sampleRate, std::int64_t ptsUs);
    [[nodiscard]] std::string problem() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
