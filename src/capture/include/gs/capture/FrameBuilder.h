#pragma once

#include "gs/capture/CaptureSource.h"

#include <gs/media/VideoDecoder.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct ID3D11Device;

namespace gs::capture {

void bgraToNv12(const std::uint8_t* pixels, std::ptrdiff_t pitch, int width, int height, std::uint8_t* luma,
                std::ptrdiff_t lumaPitch, std::uint8_t* chroma, std::ptrdiff_t chromaPitch) noexcept;

class FrameBuilder
{
public:
    using Deliver = std::function<void(media::VideoFrame frame)>;

    FrameBuilder(ID3D11Device* device, Deliver deliver);
    ~FrameBuilder();
    FrameBuilder(const FrameBuilder&) = delete;
    FrameBuilder& operator=(const FrameBuilder&) = delete;

    void build(const CaptureSample& sample);
    void reset();
    [[nodiscard]] std::string problem() const;
    [[nodiscard]] std::uint64_t dropped() const noexcept;
    [[nodiscard]] std::uint16_t session() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
