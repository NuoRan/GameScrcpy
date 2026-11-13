#include "VideoGeometry.h"

#include <gs/media/VideoDecoder.h>
#include <gs/render/FrameSlot.h>
#include <gs/render/VideoItem.h>

#include <doctest/doctest.h>

#include <QRectF>
#include <QSGNode>
#include <QSGRenderNode>
#include <QSize>

#include <cstdint>
#include <memory>
#include <vector>

using gs::media::VideoFrame;
using gs::render::croppedSize;
using gs::render::fitted;
using gs::render::FrameSlot;
using gs::render::VideoItem;

namespace {

VideoFrame makeFrame(int width, int height, std::int64_t ptsUs, std::int64_t receivedUs = 0)
{
    const auto chromaPitch = (width + 1) / 2 * 2;
    std::vector<std::uint8_t> luma(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 16);
    std::vector<std::uint8_t> chroma(static_cast<std::size_t>(chromaPitch) * static_cast<std::size_t>((height + 1) / 2),
                                     128);
    return VideoFrame::copyNv12(luma.data(), width, chroma.data(), chromaPitch, width, height, ptsUs, 1, receivedUs);
}

class PaintedVideo final : public VideoItem
{
public:
    PaintedVideo() = default;
    ~PaintedVideo() override { delete m_node; }
    PaintedVideo(const PaintedVideo&) = delete;
    PaintedVideo& operator=(const PaintedVideo&) = delete;

    void paint()
    {
        m_node = updatePaintNode(m_node, nullptr);
        static_cast<QSGRenderNode*>(m_node)->prepare();
        m_node = updatePaintNode(m_node, nullptr);
    }

private:
    QSGNode* m_node = nullptr;
};

}

TEST_CASE("ADR-0114: the node takes one frame per prepare, and a queued second one next")
{
    const auto slot = std::make_shared<FrameSlot>();
    PaintedVideo video;
    video.setSource(slot);
    slot->setDisplayIntervalUs(0);
    CHECK(video.frameSize().isEmpty());
    slot->put(makeFrame(4, 8, 1'000, 500));
    slot->put(makeFrame(8, 4, 2'000, 500));
    CHECK(slot->taken() == 0);
    video.paint();
    CHECK(slot->taken() == 1);
    CHECK(video.frameSize() == QSize(4, 8));
    CHECK(slot->pending());
    video.paint();
    CHECK(slot->taken() == 2);
    CHECK(video.frameSize() == QSize(8, 4));
    CHECK_FALSE(slot->pending());
}

TEST_CASE("ADR-0114: a picture is fitted and cropped before fitting")
{
    CHECK(fitted(QSize(), QRectF(0.0, 0.0, 100.0, 100.0)).isEmpty());
    CHECK(fitted(QSize(100, 100), QRectF()).isEmpty());
    CHECK(fitted(QSize(1080, 1920), QRectF(0.0, 0.0, 1080.0, 1920.0)) == QRectF(0.0, 0.0, 1080.0, 1920.0));
    CHECK(fitted(QSize(1080, 1920), QRectF(0.0, 0.0, 1920.0, 1080.0)) == QRectF(656.25, 0.0, 607.5, 1080.0));
    CHECK(fitted(QSize(1080, 1920), QRectF(0.0, 0.0, 1080.0, 1080.0)) == QRectF(236.25, 0.0, 607.5, 1080.0));
    CHECK(fitted(QSize(1920, 1080), QRectF(0.0, 0.0, 1920.0, 1080.0)) == QRectF(0.0, 0.0, 1920.0, 1080.0));
    CHECK(fitted(QSize(1920, 1080), QRectF(0.0, 0.0, 1080.0, 1920.0)) == QRectF(0.0, 656.25, 1080.0, 607.5));
    CHECK(fitted(QSize(1920, 1080), QRectF(0.0, 0.0, 1080.0, 1080.0)) == QRectF(0.0, 236.25, 1080.0, 607.5));
    CHECK(croppedSize(QSize(), QRectF(0.0, 0.0, 0.5, 1.0)).isEmpty());
    CHECK(croppedSize(QSize(1920, 1080), QRectF(0.0, 0.0, 0.5, 1.0)) == QSize(960, 1080));
    CHECK(croppedSize(QSize(1920, 1080), QRectF(0.0, 0.0, 1.0, 0.5)) == QSize(1920, 540));
    CHECK(fitted(croppedSize(QSize(1920, 1080), QRectF(0.5, 0.0, 0.5, 1.0)), QRectF(0.0, 0.0, 480.0, 540.0)) ==
          QRectF(0.0, 0.0, 480.0, 540.0));
}
