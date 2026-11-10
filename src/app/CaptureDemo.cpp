#include "CaptureDemo.h"

#include <gs/media/FrameUploader.h>
#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Constants.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <d3d11.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <utility>

namespace gs::app {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::int64_t kFallbackFrameUs = 33'333;
constexpr std::int64_t kLongestGapUs = 5'000'000;
constexpr std::int64_t kUnknownGap = -1;
constexpr auto kLongestCatchUp = std::chrono::milliseconds(100);
constexpr int kInnerDepth = 3;

media::VideoColor colorOf(const AVFrame& frame) noexcept
{
    media::VideoColor color;
    color.fullRange = frame.color_range == AVCOL_RANGE_JPEG;
    if (frame.colorspace == AVCOL_SPC_BT709) {
        color.matrix = media::VideoMatrix::Bt709;
    } else if (frame.colorspace == AVCOL_SPC_SMPTE170M || frame.colorspace == AVCOL_SPC_BT470BG || frame.height < 720) {
        color.matrix = media::VideoMatrix::Bt601;
    } else {
        color.matrix = media::VideoMatrix::Bt709;
    }
    return color;
}

class Framer final : public media::FrameSink
{
public:
    Framer(ID3D11Device* device, capture::SampleSink& sink) : m_inner(device, kInnerDepth), m_outer(device), m_sink(sink)
    {
    }

    void setShift(std::int64_t shift) noexcept { m_shift = shift; }

    void onFrame(media::VideoFrame frame) override
    {
        const AVFrame* picture = frame.avFrame();
        if (picture == nullptr) {
            return;
        }
        const media::FrameStamp stamp{.ptsUs = frame.ptsUs() + m_shift,
                                      .session = 1,
                                      .receivedUs = frame.receivedUs(),
                                      .color = colorOf(*picture)};
        media::VideoFrame uploaded;
        if (picture->format != AV_PIX_FMT_D3D11) {
            uploaded = upload(*picture, stamp);
            picture = uploaded.avFrame();
            if (picture == nullptr || picture->format != AV_PIX_FMT_D3D11) {
                return;
            }
        }
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(picture->data[0]);
        const auto slice = static_cast<UINT>(reinterpret_cast<std::intptr_t>(picture->data[1]));
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        media::VideoFrame framed =
            m_outer.fitted(texture, D3D11CalcSubresource(0, slice, description.MipLevels), picture->width,
                           picture->height, CaptureDemoSource::kOuterWidth, CaptureDemoSource::kOuterHeight, stamp);
        if (framed) {
            m_sink.frame(std::move(framed));
        }
    }

private:
    media::VideoFrame upload(const AVFrame& picture, const media::FrameStamp& stamp)
    {
        if (picture.format == AV_PIX_FMT_NV12) {
            return m_inner.nv12({picture.data[0], picture.linesize[0], picture.data[1], picture.linesize[1]},
                                picture.width, picture.height, stamp);
        }
        if (picture.format != AV_PIX_FMT_YUV420P && picture.format != AV_PIX_FMT_YUVJ420P) {
            return {};
        }
        const int chromaWidth = (picture.width + 1) / 2;
        const int chromaHeight = (picture.height + 1) / 2;
        m_chroma.resize(static_cast<std::size_t>(2 * chromaWidth) * static_cast<std::size_t>(chromaHeight));
        for (int row = 0; row < chromaHeight; ++row) {
            const std::uint8_t* cb = picture.data[1] + static_cast<std::ptrdiff_t>(row) * picture.linesize[1];
            const std::uint8_t* cr = picture.data[2] + static_cast<std::ptrdiff_t>(row) * picture.linesize[2];
            std::uint8_t* out = m_chroma.data() + static_cast<std::ptrdiff_t>(row) * 2 * chromaWidth;
            for (int column = 0; column < chromaWidth; ++column) {
                out[2 * column] = cb[column];
                out[2 * column + 1] = cr[column];
            }
        }
        return m_inner.nv12({picture.data[0], picture.linesize[0], m_chroma.data(), 2 * chromaWidth}, picture.width,
                            picture.height, stamp);
    }

    media::FrameUploader m_inner;
    media::FrameUploader m_outer;
    capture::SampleSink& m_sink;
    std::vector<std::uint8_t> m_chroma;
    std::int64_t m_shift = 0;
};

}

CaptureDemoSource::CaptureDemoSource(ID3D11Device* device, std::filesystem::path recording)
    : m_device(device), m_recording(std::move(recording))
{
}

CaptureDemoSource::~CaptureDemoSource()
{
    stop();
}

bool CaptureDemoSource::start(capture::SampleSink& sink, const std::string&)
{
    stop();
    if (std::string reason = load(); !reason.empty()) {
        fail(std::move(reason));
        return false;
    }
    fail({});
    m_player = std::jthread([this, &sink](const std::stop_token& stop) { run(stop, sink); });
    return true;
}

void CaptureDemoSource::stop()
{
    if (m_player.joinable()) {
        m_player.request_stop();
        m_player.join();
    }
}

std::string CaptureDemoSource::problem() const
{
    const std::scoped_lock lock(m_mutex);
    return m_problem;
}

void CaptureDemoSource::fail(std::string reason)
{
    const std::scoped_lock lock(m_mutex);
    m_problem = std::move(reason);
}

std::string CaptureDemoSource::load()
{
    m_packets.clear();
    m_delaysUs.clear();
    m_bytes.clear();
    std::ifstream file(m_recording, std::ios::binary);
    if (!file) {
        return "the recording for the demo capture card cannot be read";
    }
    m_bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    std::optional<std::uint16_t> session;
    bool configured = false;
    std::optional<std::int64_t> previousPts;
    std::optional<std::int64_t> firstPts;
    std::int64_t lastPts = 0;
    std::int64_t gapSum = 0;
    std::int64_t gaps = 0;
    for (std::size_t offset = 0; offset < m_bytes.size();) {
        auto packet = protocol::decodeMediaPacket(protocol::MediaChannel::Video, std::span(m_bytes).subspan(offset));
        if (!packet) {
            m_packets.clear();
            return "the file for the demo capture card is not a video recording";
        }
        const protocol::MediaHeader header = packet->header;
        std::int64_t delay = 0;
        if (header.kind == protocol::MediaKind::Session) {
            session = header.session;
            configured = false;
        } else if (!session || header.session != *session ||
                   (header.kind == protocol::MediaKind::Frame && !configured) ||
                   (header.kind == protocol::MediaKind::Frame && header.ptsUs < 0)) {
            m_packets.clear();
            return "the recording for the demo capture card has its packets out of order";
        } else if (header.kind == protocol::MediaKind::Config) {
            configured = true;
        } else {
            delay = previousPts && header.ptsUs >= *previousPts ? std::min(header.ptsUs - *previousPts, kLongestGapUs)
                                                                : kUnknownGap;
            if (delay > 0) {
                gapSum += delay;
                ++gaps;
            }
            previousPts = header.ptsUs;
            firstPts = firstPts.value_or(header.ptsUs);
            lastPts = std::max(lastPts, header.ptsUs);
        }
        offset += protocol::kMediaHeaderSize + header.size;
        m_packets.push_back(std::move(*packet));
        m_delaysUs.push_back(delay);
    }
    if (!previousPts) {
        m_packets.clear();
        return "the recording for the demo capture card holds no picture";
    }
    const std::int64_t typical = gaps > 0 ? gapSum / gaps : kFallbackFrameUs;
    std::ranges::replace(m_delaysUs, kUnknownGap, typical);
    m_loopUs = std::max(lastPts - *firstPts, std::int64_t{0}) + typical;
    return {};
}

void CaptureDemoSource::run(const std::stop_token& stop, capture::SampleSink& sink)
{
    media::VideoDecoder decoder(m_device);
    Framer framer(m_device, sink);
    const capture::CaptureFormat format{.width = kOuterWidth,
                                        .height = kOuterHeight,
                                        .rateNumerator = 60,
                                        .rateDenominator = 1,
                                        .subtype = capture::Subtype::Nv12};
    sink.offered({format}, format);
    const auto broken = [&](const media::VideoDecodeError& error) {
        std::string reason = "the demo recording cannot be decoded: " + error.message;
        fail(reason);
        sink.ended(reason);
    };
    std::size_t next = 0;
    for (; next < m_packets.size() && m_packets[next].header.kind != protocol::MediaKind::Frame; ++next) {
        if (const auto pushed = decoder.push(m_packets[next], framer); !pushed) {
            broken(pushed.error());
            return;
        }
    }
    if (next >= m_packets.size()) {
        return;
    }
    std::mutex idle;
    std::condition_variable_any wake;
    std::int64_t shift = 0;
    Clock::time_point due = Clock::now() - std::chrono::microseconds(m_delaysUs[next]);
    for (;;) {
        for (; next < m_packets.size(); ++next) {
            const protocol::MediaPacket& packet = m_packets[next];
            if (packet.header.kind == protocol::MediaKind::Frame) {
                due = std::max(due + std::chrono::microseconds(m_delaysUs[next]), Clock::now() - kLongestCatchUp);
                std::unique_lock lock(idle);
                static_cast<void>(wake.wait_until(lock, stop, due, [] { return false; }));
                if (stop.stop_requested()) {
                    return;
                }
            }
            framer.setShift(shift);
            if (const auto pushed = decoder.push(packet, framer); !pushed) {
                broken(pushed.error());
                return;
            }
        }
        next = 0;
        shift += m_loopUs;
    }
}

}
