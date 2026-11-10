#pragma once

#include "gs/capture/CaptureFormat.h"

#include <gs/media/VideoDecoder.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct ID3D11Texture2D;

namespace gs::capture {

enum class SampleKind : std::uint8_t { Nv12, Yuy2, Mjpg, H264, Texture };

struct CaptureSample {
    SampleKind kind = SampleKind::Nv12;
    int width = 0;
    int height = 0;
    std::span<const std::uint8_t> bytes;
    std::ptrdiff_t pitch = 0;
    std::ptrdiff_t chromaOffset = 0;
    std::span<const std::uint8_t> config;
    ID3D11Texture2D* texture = nullptr;
    unsigned subresource = 0;
    std::shared_ptr<void> owner;
    std::int64_t ptsUs = 0;
    std::int64_t receivedUs = 0;
    bool keyFrame = true;
    media::VideoColor color;
};

class SampleSink
{
public:
    virtual ~SampleSink() = default;

    virtual void offered(const std::vector<CaptureFormat>& formats, const std::optional<CaptureFormat>& chosen) = 0;
    virtual void sample(const CaptureSample& sample) = 0;
    virtual void frame(media::VideoFrame frame) = 0;
    virtual void ended(const std::string& problem) = 0;
};

class CaptureSource
{
public:
    virtual ~CaptureSource() = default;

    [[nodiscard]] virtual bool start(SampleSink& sink, const std::string& format) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual std::string problem() const = 0;
    [[nodiscard]] virtual std::wstring container() const { return {}; }
};

}
