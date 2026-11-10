#include "gs/media/FrameUploader.h"

#include "gs/media/FrameSnapshot.h"
#include "gs/media/VideoDecoder.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <d3d11.h>
#include <wrl/client.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

using gs::media::FrameSnapshots;
using gs::media::FrameStamp;
using gs::media::FrameUploader;
using gs::media::LumaSnapshot;
using gs::media::Nv12Planes;
using gs::media::VideoColor;
using gs::media::VideoFrame;
using gs::media::VideoMatrix;
using Microsoft::WRL::ComPtr;

namespace {

ComPtr<ID3D11Device> graphicsDevice()
{
    ComPtr<ID3D11Device> device;
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                                        nullptr))) {
            return device;
        }
    }
    return nullptr;
}

std::shared_ptr<const LumaSnapshot> snapshotOf(const VideoFrame& frame, ID3D11Device* device,
                                               std::optional<std::array<double, 4>> crop = std::nullopt)
{
    FrameSnapshots snapshots(device);
    if (crop) {
        snapshots.setCrop((*crop)[0], (*crop)[1], (*crop)[2], (*crop)[3]);
    }
    snapshots.request(true);
    snapshots.offer(frame);
    snapshots.finish();
    return snapshots.latest();
}

struct Nv12 {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma;

    [[nodiscard]] Nv12Planes planes() const { return {luma.data(), width, chroma.data(), width}; }
};

Nv12 pattern(int width, int height)
{
    Nv12 picture{.width = width,
                 .height = height,
                 .luma = std::vector<std::uint8_t>(static_cast<std::size_t>(width * height)),
                 .chroma = std::vector<std::uint8_t>(static_cast<std::size_t>(width * height / 2))};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            picture.luma[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(16 + (x * 5 + y * 9) % 220);
        }
    }
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width; ++x) {
            picture.chroma[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(40 + (x * 3 + y * 17) % 170);
        }
    }
    return picture;
}

std::vector<std::uint8_t> yuy2Pattern(int width, int height, std::ptrdiff_t pitch)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(pitch * height), 0);
    for (int y = 0; y < height; ++y) {
        for (int pair = 0; pair < width / 2; ++pair) {
            const auto at = static_cast<std::size_t>(y * pitch + 4 * pair);
            bytes[at] = static_cast<std::uint8_t>(16 + (2 * pair * 7 + y * 3) % 220);
            bytes[at + 1] = static_cast<std::uint8_t>(64 + (pair * 5 + y * 11) % 128);
            bytes[at + 2] = static_cast<std::uint8_t>(16 + ((2 * pair + 1) * 7 + y * 3) % 220);
            bytes[at + 3] = static_cast<std::uint8_t>(32 + (pair * 13 + y * 7) % 160);
        }
    }
    return bytes;
}

int worstDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b)
{
    REQUIRE(a.size() == b.size());
    int worst = 0;
    for (std::size_t index = 0; index < a.size(); ++index) {
        worst = std::max(worst, std::abs(a[index] - b[index]));
    }
    return worst;
}

}

TEST_CASE("the CPU reference turns YUY2 into NV12: luma as it is, chroma averaged over two rows")
{
    const std::vector<std::uint8_t> packed{
        10, 100, 20, 200, 30, 101, 40, 201,
        50, 110, 60, 210, 70, 112, 80, 214,
        90, 120, 91, 220, 92, 121, 93, 221,
    };
    std::vector<std::uint8_t> luma(4 * 3, 0);
    std::vector<std::uint8_t> chroma(4 * 2, 0);
    gs::media::yuy2ToNv12(packed.data(), 8, 4, 3, luma.data(), 4, chroma.data(), 4);
    CHECK(luma == std::vector<std::uint8_t>{10, 20, 30, 40, 50, 60, 70, 80, 90, 91, 92, 93});
    CHECK(chroma == std::vector<std::uint8_t>{105, 205, 107, 208, 120, 220, 121, 221});
}

TEST_CASE("the frame uploader turns YUY2 into the same NV12 as the CPU reference, and snapshots can crop it (MED-014)")
{
    const ComPtr<ID3D11Device> device = graphicsDevice();
    if (!device) {
        MESSAGE("no Direct3D 11 device: skipped");
        return;
    }
    constexpr int kWidth = 96;
    constexpr int kHeight = 54;
    const std::ptrdiff_t pitch = 2 * kWidth + 32;
    const std::vector<std::uint8_t> packed = yuy2Pattern(kWidth, kHeight, pitch);
    std::vector<std::uint8_t> luma(static_cast<std::size_t>(kWidth * kHeight));
    std::vector<std::uint8_t> chroma(static_cast<std::size_t>(kWidth * kHeight / 2));
    gs::media::yuy2ToNv12(packed.data(), pitch, kWidth, kHeight, luma.data(), kWidth, chroma.data(), kWidth);

    FrameUploader uploader(device.Get());
    if (!uploader.drawsOnGpu()) {
        MESSAGE("this device cannot draw into NV12: the CPU conversion is checked instead");
    }
    const VideoFrame frame = uploader.yuy2(packed.data(), pitch, kWidth, kHeight,
                                           FrameStamp{.ptsUs = 5, .session = 3, .receivedUs = 7});
    REQUIRE(frame);
    CHECK(frame.hardware());
    CHECK(frame.width() == kWidth);
    CHECK(frame.height() == kHeight);
    CHECK(frame.ptsUs() == 5);
    CHECK(frame.session() == 3);
    CHECK(frame.receivedUs() == 7);

    const auto whole = snapshotOf(frame, device.Get());
    REQUIRE(whole != nullptr);
    REQUIRE(whole->colored());
    CHECK(whole->luma == luma);
    CHECK(worstDifference(whole->chroma, chroma) <= 1);

    const auto cropped = snapshotOf(frame, device.Get(), std::array{0.25, 0.5, 0.5, 0.5});
    REQUIRE(cropped != nullptr);
    REQUIRE(cropped->width == 48);
    REQUIRE(cropped->height == 28);
    for (int y = 0; y < cropped->height; ++y) {
        for (int x = 0; x < cropped->width; ++x) {
            CAPTURE(x);
            CAPTURE(y);
            REQUIRE(cropped->luma[static_cast<std::size_t>(y * 48 + x)] ==
                    luma[static_cast<std::size_t>((y + 26) * kWidth + x + 24)]);
        }
    }
    for (int y = 0; y < cropped->chromaHeight(); ++y) {
        for (int x = 0; x < 2 * cropped->chromaWidth(); ++x) {
            CAPTURE(x);
            CAPTURE(y);
            REQUIRE(std::abs(cropped->chroma[static_cast<std::size_t>(y * 48 + x)] -
                             chroma[static_cast<std::size_t>((y + 13) * kWidth + x + 24)]) <= 1);
        }
    }
}

TEST_CASE("the frame uploader copies NV12 planes into a small ring of textures and drops a picture when all are shown")
{
    const ComPtr<ID3D11Device> device = graphicsDevice();
    if (!device) {
        MESSAGE("no Direct3D 11 device: skipped");
        return;
    }
    const Nv12 picture = pattern(64, 36);
    FrameUploader uploader(device.Get(), 2);
    VideoFrame first = uploader.nv12(picture.planes(), 64, 36, FrameStamp{.ptsUs = 1});
    VideoFrame second = uploader.nv12(picture.planes(), 64, 36, FrameStamp{.ptsUs = 2});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.avFrame()->data[0] != second.avFrame()->data[0]);
    CHECK(uploader.dropped() == 0);
    CHECK_FALSE(uploader.nv12(picture.planes(), 64, 36, FrameStamp{.ptsUs = 3}));
    CHECK(uploader.dropped() == 1);

    const auto shot = snapshotOf(first, device.Get());
    REQUIRE(shot != nullptr);
    CHECK(shot->luma == picture.luma);
    CHECK(shot->chroma == picture.chroma);

    const auto* reused = first.avFrame()->data[0];
    first = VideoFrame();
    const VideoFrame third = uploader.nv12(picture.planes(), 64, 36, FrameStamp{.ptsUs = 4});
    REQUIRE(third);
    CHECK(third.avFrame()->data[0] == reused);
    CHECK(third.ptsUs() == 4);

    FrameUploader software(nullptr);
    const VideoFrame inMemory = software.nv12(picture.planes(), 64, 36, FrameStamp{.ptsUs = 9});
    REQUIRE(inMemory);
    CHECK_FALSE(inMemory.hardware());
    CHECK(inMemory.avFrame()->format == AV_PIX_FMT_NV12);
    CHECK_FALSE(software.drawsOnGpu());
}

TEST_CASE("the frame uploader draws a picture into the middle of a larger black frame, for the demo capture card")
{
    const ComPtr<ID3D11Device> device = graphicsDevice();
    if (!device) {
        MESSAGE("no Direct3D 11 device: skipped");
        return;
    }
    FrameUploader inner(device.Get(), 2);
    FrameUploader outer(device.Get(), 2);
    if (!outer.drawsOnGpu()) {
        MESSAGE("this device cannot draw into NV12: skipped");
        return;
    }
    Nv12 portrait{.width = 36, .height = 64,
                  .luma = std::vector<std::uint8_t>(36 * 64, 200),
                  .chroma = std::vector<std::uint8_t>(36 * 32, 0)};
    for (std::size_t index = 0; index < portrait.chroma.size(); index += 2) {
        portrait.chroma[index] = 90;
        portrait.chroma[index + 1] = 160;
    }
    const VideoFrame source = inner.nv12(portrait.planes(), 36, 64, FrameStamp{.ptsUs = 11});
    REQUIRE(source);
    auto* texture = reinterpret_cast<ID3D11Texture2D*>(source.avFrame()->data[0]);
    const VideoFrame fitted = outer.fitted(texture, 0, 36, 64, 128, 72, FrameStamp{.ptsUs = 11, .session = 2});
    REQUIRE(fitted);
    CHECK(fitted.width() == 128);
    CHECK(fitted.height() == 72);
    CHECK(fitted.session() == 2);
    const auto shot = snapshotOf(fitted, device.Get());
    REQUIRE(shot != nullptr);
    const auto lumaAt = [&shot](int x, int y) { return static_cast<int>(shot->luma[static_cast<std::size_t>(y * 128 + x)]); };
    const auto chromaAt = [&shot](int x, int y, int part) {
        return static_cast<int>(shot->chroma[static_cast<std::size_t>(y * 128 + 2 * x + part)]);
    };
    CHECK(lumaAt(10, 36) == 16);
    CHECK(lumaAt(120, 36) == 16);
    CHECK(std::abs(lumaAt(64, 36) - 200) <= 2);
    CHECK(std::abs(lumaAt(64, 2) - 200) <= 2);
    CHECK(chromaAt(4, 18, 0) == 128);
    CHECK(chromaAt(60, 18, 1) == 128);
    CHECK(std::abs(chromaAt(32, 18, 0) - 90) <= 2);
    CHECK(std::abs(chromaAt(32, 18, 1) - 160) <= 2);
}

TEST_CASE("video frames made from NV12 in memory keep the planes, times and colour description")
{
    const Nv12 picture = pattern(10, 6);
    const VideoFrame frame = VideoFrame::copyNv12(picture.luma.data(), 10, picture.chroma.data(), 10, 10, 6, 123, 4,
                                                  456, VideoColor{.matrix = VideoMatrix::Bt709, .fullRange = true});
    REQUIRE(frame);
    CHECK_FALSE(frame.hardware());
    CHECK(frame.width() == 10);
    CHECK(frame.height() == 6);
    CHECK(frame.ptsUs() == 123);
    CHECK(frame.session() == 4);
    CHECK(frame.receivedUs() == 456);
    const AVFrame& raw = *frame.avFrame();
    CHECK(raw.format == AV_PIX_FMT_NV12);
    CHECK(raw.colorspace == AVCOL_SPC_BT709);
    CHECK(raw.color_range == AVCOL_RANGE_JPEG);
    for (int y = 0; y < 6; ++y) {
        CHECK(std::equal(raw.data[0] + y * raw.linesize[0], raw.data[0] + y * raw.linesize[0] + 10,
                         picture.luma.begin() + y * 10));
    }
    for (int y = 0; y < 3; ++y) {
        CHECK(std::equal(raw.data[1] + y * raw.linesize[1], raw.data[1] + y * raw.linesize[1] + 10,
                         picture.chroma.begin() + y * 10));
    }
    const VideoFrame studio = VideoFrame::copyNv12(picture.luma.data(), 10, picture.chroma.data(), 10, 10, 6, 0, 0, 0);
    CHECK(studio.avFrame()->color_range == AVCOL_RANGE_MPEG);
    CHECK(studio.avFrame()->colorspace == AVCOL_SPC_UNSPECIFIED);
    CHECK_FALSE(VideoFrame::copyNv12(nullptr, 10, picture.chroma.data(), 10, 10, 6, 0, 0, 0));
    CHECK_FALSE(VideoFrame::copyNv12(picture.luma.data(), 10, picture.chroma.data(), 10, 0, 6, 0, 0, 0));
}

TEST_CASE("a video frame wrapping a texture keeps its owner alive until the last share is gone")
{
    const ComPtr<ID3D11Device> device = graphicsDevice();
    if (!device) {
        MESSAGE("no Direct3D 11 device: skipped");
        return;
    }
    D3D11_TEXTURE2D_DESC layout{};
    layout.Width = 16;
    layout.Height = 8;
    layout.MipLevels = 1;
    layout.ArraySize = 4;
    layout.Format = DXGI_FORMAT_R8_UNORM;
    layout.SampleDesc.Count = 1;
    layout.Usage = D3D11_USAGE_DEFAULT;
    layout.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&layout, nullptr, &texture)));

    auto owner = std::make_shared<int>(1);
    const std::weak_ptr<int> watched = owner;
    VideoFrame frame = VideoFrame::wrapTexture(texture.Get(), 2, 16, 8, owner, 100, 5, 200,
                                               VideoColor{.matrix = VideoMatrix::Bt601});
    owner.reset();
    REQUIRE(frame);
    CHECK(frame.hardware());
    CHECK(frame.avFrame()->data[0] == reinterpret_cast<std::uint8_t*>(texture.Get()));
    CHECK(reinterpret_cast<std::intptr_t>(frame.avFrame()->data[1]) == 2);
    CHECK(frame.ptsUs() == 100);
    CHECK(frame.session() == 5);
    CHECK(frame.receivedUs() == 200);
    CHECK(frame.avFrame()->colorspace == AVCOL_SPC_SMPTE170M);
    CHECK_FALSE(watched.expired());

    VideoFrame shared = frame.share();
    frame = VideoFrame();
    CHECK_FALSE(watched.expired());
    REQUIRE(shared);
    CHECK(shared.avFrame()->data[0] == reinterpret_cast<std::uint8_t*>(texture.Get()));
    shared = VideoFrame();
    CHECK(watched.expired());

    CHECK_FALSE(VideoFrame::wrapTexture(nullptr, 0, 16, 8, nullptr, 0, 0, 0));
    CHECK_FALSE(VideoFrame::wrapTexture(texture.Get(), 0, 0, 8, nullptr, 0, 0, 0));
}

TEST_CASE("a video frame adopts a picture made elsewhere with the session and arrival time it is given")
{
    AVFrame* picture = av_frame_alloc();
    REQUIRE(picture != nullptr);
    picture->format = AV_PIX_FMT_NV12;
    picture->width = 8;
    picture->height = 4;
    REQUIRE(av_frame_get_buffer(picture, 0) >= 0);
    picture->pts = 77;
    const VideoFrame frame = VideoFrame::adopt(picture, 9, 1'234);
    REQUIRE(frame);
    CHECK(frame.avFrame() == picture);
    CHECK(frame.width() == 8);
    CHECK(frame.ptsUs() == 77);
    CHECK(frame.session() == 9);
    CHECK(frame.receivedUs() == 1'234);
}

TEST_CASE("frame snapshots of an in-memory picture can be cropped to the phone screen and uncropped again")
{
    const Nv12 picture = pattern(64, 36);
    const VideoFrame frame = VideoFrame::copyNv12(picture.luma.data(), 64, picture.chroma.data(), 64, 64, 36, 1, 1, 1);
    REQUIRE(frame);
    FrameSnapshots snapshots;
    snapshots.setCrop(0.25, 0.0, 0.5, 1.0);
    snapshots.request(true);
    snapshots.offer(frame);
    const auto cropped = snapshots.latest();
    REQUIRE(cropped != nullptr);
    CHECK(cropped->width == 32);
    CHECK(cropped->height == 36);
    CHECK(cropped->luma[0] == picture.luma[16]);
    CHECK(cropped->luma[35 * 32 + 31] == picture.luma[35 * 64 + 47]);
    CHECK(cropped->chroma[0] == picture.chroma[16]);
    CHECK(cropped->chroma[17 * 32 + 31] == picture.chroma[17 * 64 + 47]);

    const auto captured = snapshots.capture();
    REQUIRE(captured != nullptr);
    CHECK(captured->width == 32);
    CHECK(captured->luma[0] == picture.luma[16]);

    snapshots.clearCrop();
    snapshots.request();
    snapshots.offer(frame);
    const auto whole = snapshots.latest();
    REQUIRE(whole != nullptr);
    CHECK(whole->width == 64);
    CHECK(whole->luma == picture.luma);
}
