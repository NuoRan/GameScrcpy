#pragma once

#include "gs/media/VideoDecoder.h"

#include <cstddef>
#include <cstdint>
#include <memory>

struct ID3D11Device;
struct ID3D11Texture2D;

namespace gs::media {

struct Nv12Planes {
    const std::uint8_t* luma = nullptr;
    std::ptrdiff_t lumaPitch = 0;
    const std::uint8_t* chroma = nullptr;
    std::ptrdiff_t chromaPitch = 0;
};

struct FrameStamp {
    std::int64_t ptsUs = 0;
    std::uint16_t session = 0;
    std::int64_t receivedUs = 0;
    VideoColor color;
};

class FrameUploader
{
public:
    static constexpr int kDefaultDepth = 6;

    explicit FrameUploader(ID3D11Device* device, int depth = kDefaultDepth);
    ~FrameUploader();
    FrameUploader(const FrameUploader&) = delete;
    FrameUploader& operator=(const FrameUploader&) = delete;

    [[nodiscard]] VideoFrame nv12(const Nv12Planes& planes, int width, int height, const FrameStamp& stamp);
    [[nodiscard]] VideoFrame yuy2(const std::uint8_t* pixels, std::ptrdiff_t pitch, int width, int height,
                                  const FrameStamp& stamp);
    [[nodiscard]] VideoFrame copy(ID3D11Texture2D* source, unsigned subresource, int width, int height,
                                  const FrameStamp& stamp);
    [[nodiscard]] VideoFrame fitted(ID3D11Texture2D* source, unsigned subresource, int width, int height,
                                    int outerWidth, int outerHeight, const FrameStamp& stamp);
    [[nodiscard]] bool drawsOnGpu();
    [[nodiscard]] std::uint64_t dropped() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

void yuy2ToNv12(const std::uint8_t* pixels, std::ptrdiff_t pitch, int width, int height, std::uint8_t* luma,
                std::ptrdiff_t lumaPitch, std::uint8_t* chroma, std::ptrdiff_t chromaPitch) noexcept;

}
