#pragma once

#include "gs/capture/CaptureSource.h"

#include <gs/media/VideoDecoder.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct ID3D11Device;

namespace gs::capture {

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
