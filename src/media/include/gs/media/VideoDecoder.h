#pragma once

#include "gs/base/Expected.h"
#include "gs/base/ThreadChecker.h"
#include "gs/protocol/Codec.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct AVFrame;
struct ID3D11Device;
struct ID3D11Texture2D;

namespace gs::media {

enum class VideoMatrix : std::uint8_t { Automatic, Bt601, Bt709 };

struct VideoColor {
    VideoMatrix matrix = VideoMatrix::Automatic;
    bool fullRange = false;

    friend bool operator==(const VideoColor&, const VideoColor&) = default;
};

class VideoFrame
{
public:
    [[nodiscard]] static VideoFrame adopt(AVFrame* frame, std::uint16_t session, std::int64_t receivedUs) noexcept;
    [[nodiscard]] static VideoFrame wrapTexture(ID3D11Texture2D* texture, unsigned slice, int width, int height,
                                                std::shared_ptr<void> owner, std::int64_t ptsUs,
                                                std::uint16_t session, std::int64_t receivedUs, VideoColor color = {});
    [[nodiscard]] static VideoFrame copyNv12(const std::uint8_t* luma, std::ptrdiff_t lumaPitch,
                                             const std::uint8_t* chroma, std::ptrdiff_t chromaPitch, int width,
                                             int height, std::int64_t ptsUs, std::uint16_t session,
                                             std::int64_t receivedUs, VideoColor color = {});

    VideoFrame() noexcept = default;
    VideoFrame(VideoFrame&& other) noexcept;
    VideoFrame& operator=(VideoFrame&& other) noexcept;
    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;
    ~VideoFrame();

    [[nodiscard]] explicit operator bool() const noexcept { return m_frame != nullptr; }
    [[nodiscard]] bool hardware() const noexcept;
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] std::int64_t ptsUs() const noexcept;
    [[nodiscard]] std::uint16_t session() const noexcept { return m_session; }
    [[nodiscard]] std::int64_t receivedUs() const noexcept { return m_receivedUs; }
    [[nodiscard]] const AVFrame* avFrame() const noexcept { return m_frame; }
    [[nodiscard]] VideoFrame share() const;

private:
    friend class VideoDecoder;
    VideoFrame(AVFrame* frame, std::uint16_t session, std::int64_t receivedUs) noexcept
        : m_frame(frame), m_session(session), m_receivedUs(receivedUs)
    {
    }

    AVFrame* m_frame = nullptr;
    std::uint16_t m_session = 0;
    std::int64_t m_receivedUs = 0;
};

class FrameSink
{
public:
    virtual ~FrameSink() = default;
    virtual void onFrame(VideoFrame frame) = 0;
};

enum class VideoDecodeErrorKind : std::uint8_t {
    OutOfOrder,
    Unsupported,
    Failed,
};

struct VideoDecodeError {
    VideoDecodeErrorKind kind = VideoDecodeErrorKind::Failed;
    std::string message;
};

class VideoDecoder
{
public:
    explicit VideoDecoder(ID3D11Device* device = nullptr);
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    [[nodiscard]] Expected<void, VideoDecodeError> push(const protocol::MediaPacket& packet, FrameSink& sink);

    [[nodiscard]] bool hardware() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
    ThreadChecker m_thread;
};

}
