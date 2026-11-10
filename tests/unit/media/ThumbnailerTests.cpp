#include "gs/media/Thumbnailer.h"

#include "gs/media/VideoDecoder.h"

#include <gs/protocol/Constants.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <d3d11.h>
#include <wrl/client.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

using namespace gs;
using gs::media::FrameSink;
using gs::media::Thumbnail;
using gs::media::Thumbnailer;
using gs::media::VideoDecoder;
using gs::media::VideoFrame;
using Microsoft::WRL::ComPtr;

namespace {

AVFrame* solidFrame(int width, int height, std::uint8_t y, std::uint8_t cb, std::uint8_t cr)
{
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    frame->colorspace = AVCOL_SPC_SMPTE170M;
    frame->color_range = AVCOL_RANGE_MPEG;
    REQUIRE(av_frame_get_buffer(frame, 0) >= 0);
    for (int row = 0; row < height; ++row) {
        std::fill_n(frame->data[0] + row * frame->linesize[0], width, y);
    }
    for (int row = 0; row < (height + 1) / 2; ++row) {
        std::fill_n(frame->data[1] + row * frame->linesize[1], (width + 1) / 2, cb);
        std::fill_n(frame->data[2] + row * frame->linesize[2], (width + 1) / 2, cr);
    }
    return frame;
}

int channel(std::uint32_t pixel, int shift)
{
    return static_cast<int>((pixel >> shift) & 0xFF);
}

std::vector<std::uint8_t> readRecording(const char* name)
{
    std::ifstream file(std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / name, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Thumbnails final : FrameSink {
    explicit Thumbnails(ID3D11Device* device) : thumbnailer(device) {}

    void onFrame(VideoFrame frame) override
    {
        hardware = hardware || frame.hardware();
        const auto thumbnail = thumbnailer.make(frame, 160);
        REQUIRE(thumbnail);
        made.push_back(*thumbnail);
    }

    Thumbnailer thumbnailer;
    std::vector<Thumbnail> made;
    bool hardware = false;
};

std::vector<Thumbnail> thumbnailsOf(const char* recordingName, ID3D11Device* device, bool& hardware)
{
    const auto recording = readRecording(recordingName);
    VideoDecoder decoder(device);
    Thumbnails sink(device);
    for (std::size_t offset = 0; offset < recording.size();) {
        auto packet =
            protocol::decodeMediaPacket(protocol::MediaChannel::Video, std::span(recording).subspan(offset));
        REQUIRE(packet);
        offset += protocol::kMediaHeaderSize + packet->header.size;
        REQUIRE(decoder.push(*packet, sink));
    }
    hardware = sink.hardware;
    return sink.made;
}

}

TEST_CASE("thumbnails: the long side as asked, the colors as the renderer's matrix gives them")
{
    Thumbnailer thumbnailer;
    struct Case {
        std::uint8_t y, cb, cr;
        int red, green, blue;
    };
    for (const Case& color : {Case{16, 128, 128, 0, 0, 0}, Case{235, 128, 128, 255, 255, 255},
                              Case{81, 90, 240, 255, 0, 0}}) {
        AVFrame* frame = solidFrame(64, 128, color.y, color.cb, color.cr);
        const auto thumbnail = thumbnailer.make(*frame, 32);
        av_frame_free(&frame);
        REQUIRE(thumbnail);
        CHECK(thumbnail->height == 32);
        CHECK(thumbnail->width == 16);
        const std::uint32_t pixel = thumbnail->pixels.front();
        CHECK(channel(pixel, 24) == 0xFF);
        CHECK(std::abs(channel(pixel, 16) - color.red) <= 2);
        CHECK(std::abs(channel(pixel, 8) - color.green) <= 2);
        CHECK(std::abs(channel(pixel, 0) - color.blue) <= 2);
    }
}

TEST_CASE("thumbnails: none for formats the decoder does not make")
{
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_RGBA;
    frame->width = 8;
    frame->height = 8;
    REQUIRE(av_frame_get_buffer(frame, 0) >= 0);
    CHECK_FALSE(Thumbnailer().make(*frame, 4));
    av_frame_free(&frame);
}

TEST_CASE("thumbnails: a picture the GPU decoded gives the same thumbnail as decoded in software")
{
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        MESSAGE("no GPU with video support: skipped");
        return;
    }
    bool hardware = false;
    const auto fromGpu = thumbnailsOf("emulator_h264.media", device.Get(), hardware);
    if (!hardware) {
        MESSAGE("the GPU does not decode H.264: skipped");
        return;
    }
    bool software = true;
    const auto fromSoftware = thumbnailsOf("emulator_h264.media", nullptr, software);
    CHECK_FALSE(software);
    REQUIRE(fromGpu.size() == fromSoftware.size());
    REQUIRE_FALSE(fromGpu.empty());
    for (std::size_t i = 0; i < fromGpu.size(); ++i) {
        CAPTURE(i);
        CHECK(fromGpu[i] == fromSoftware[i]);
    }
    CHECK(fromGpu.front().height == 160);
}

namespace {

struct LaterThumbnails final : FrameSink {
    explicit LaterThumbnails(ID3D11Device* device) : later(device), atOnce(device) {}

    void onFrame(VideoFrame frame) override
    {
        hardware = hardware || frame.hardware();
        if (auto arrived = later.collect()) {
            collected.push_back(*arrived);
        }
        if (frames++ % 7 == 0) {
            expected.push_back(*atOnce.make(frame, 160));
            later.request(frame, 160);
            ++requested;
        }
    }

    Thumbnailer later;
    Thumbnailer atOnce;
    std::vector<Thumbnail> collected;
    std::vector<Thumbnail> expected;
    int frames = 0;
    int requested = 0;
    bool hardware = false;
};

}

TEST_CASE("thumbnails: asked for without waiting, a GPU picture's thumbnail arrives on a later frame unchanged")
{
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        MESSAGE("no GPU with video support: skipped");
        return;
    }
    Thumbnailer idle(device.Get());
    CHECK_FALSE(idle.collect());

    const auto recording = readRecording("emulator_h264.media");
    VideoDecoder decoder(device.Get());
    LaterThumbnails sink(device.Get());
    for (std::size_t offset = 0; offset < recording.size();) {
        auto packet =
            protocol::decodeMediaPacket(protocol::MediaChannel::Video, std::span(recording).subspan(offset));
        REQUIRE(packet);
        offset += protocol::kMediaHeaderSize + packet->header.size;
        REQUIRE(decoder.push(*packet, sink));
    }
    if (!sink.hardware) {
        MESSAGE("the GPU does not decode H.264: skipped");
        return;
    }
    for (int i = 0; i < 200 && sink.collected.size() < sink.expected.size(); ++i) {
        if (auto arrived = sink.later.collect()) {
            sink.collected.push_back(*arrived);
        }
    }
    REQUIRE(sink.requested > 2);
    REQUIRE_FALSE(sink.collected.empty());
    for (std::size_t i = 0; i < sink.collected.size(); ++i) {
        CAPTURE(i);
        const auto same = std::ranges::find(sink.expected, sink.collected[i]);
        CHECK(same != sink.expected.end());
    }
}
