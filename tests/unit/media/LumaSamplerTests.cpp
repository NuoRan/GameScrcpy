#include "gs/media/LumaSampler.h"

#include "gs/media/VideoDecoder.h"

#include <gs/protocol/Constants.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <d3d11.h>
#include <wrl/client.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

using namespace gs;
using gs::media::FrameSink;
using gs::media::LumaReading;
using gs::media::LumaSampler;
using gs::media::LumaSpot;
using gs::media::VideoDecoder;
using gs::media::VideoFrame;
using Microsoft::WRL::ComPtr;

namespace {

AVFrame* frameWithSquare(int width, int height, std::uint8_t background, std::uint8_t square, int cx, int cy, int half)
{
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    REQUIRE(av_frame_get_buffer(frame, 0) >= 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool inside = std::abs(x - cx) <= half && std::abs(y - cy) <= half;
            frame->data[0][y * frame->linesize[0] + x] = inside ? square : background;
        }
    }
    for (int row = 0; row < (height + 1) / 2; ++row) {
        std::fill_n(frame->data[1] + row * frame->linesize[1], (width + 1) / 2, std::uint8_t{128});
        std::fill_n(frame->data[2] + row * frame->linesize[2], (width + 1) / 2, std::uint8_t{128});
    }
    return frame;
}

constexpr std::array<LumaSpot, 3> kRecordingSpots{{
    {.x = 60, .y = 100, .core = 3, .ringFrom = 12, .ringTo = 18},
    {.x = 144, .y = 320, .core = 4, .ringFrom = 16, .ringTo = 22},
    {.x = 5, .y = 630, .core = 3, .ringFrom = 12, .ringTo = 18},
}};

std::vector<std::uint8_t> readRecording(const char* name)
{
    std::ifstream file(std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / name, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Readings final : FrameSink {
    explicit Readings(ID3D11Device* device) : sampler(device) {}

    void onFrame(VideoFrame frame) override
    {
        hardware = hardware || frame.hardware();
        auto read = sampler.read(frame, kRecordingSpots);
        REQUIRE(read);
        REQUIRE(read->size() == kRecordingSpots.size());
        made.push_back(std::move(*read));
    }

    LumaSampler sampler;
    std::vector<std::vector<LumaReading>> made;
    bool hardware = false;
};

std::vector<std::vector<LumaReading>> readingsOf(const char* recordingName, ID3D11Device* device, bool& hardware)
{
    const auto recording = readRecording(recordingName);
    VideoDecoder decoder(device);
    Readings sink(device);
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

TEST_CASE("luma readings: the core of a spot and the ring around it, cut at the edges of the picture")
{
    AVFrame* frame = frameWithSquare(200, 120, 40, 220, 100, 60, 4);
    LumaSampler sampler;
    const std::array<LumaSpot, 3> spots{{
        {.x = 100, .y = 60, .core = 3, .ringFrom = 20, .ringTo = 28},
        {.x = 104, .y = 60, .core = 2, .ringFrom = 20, .ringTo = 28},
        {.x = 2, .y = 3, .core = 2, .ringFrom = 6, .ringTo = 9},
    }};
    const auto readings = sampler.read(*frame, spots);
    REQUIRE(readings);
    REQUIRE(readings->size() == 3);
    CHECK((*readings)[0] == LumaReading{220, 40});
    CHECK((*readings)[1].core == doctest::Approx((220.0 * 15 + 40.0 * 10) / 25));
    CHECK((*readings)[1].ring == 40);
    CHECK((*readings)[2] == LumaReading{40, 40});

    const auto none = sampler.read(*frame, std::span<const LumaSpot>());
    REQUIRE(none);
    CHECK(none->empty());
    av_frame_free(&frame);
}

TEST_CASE("luma readings: none for formats the decoder does not make")
{
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_RGBA;
    frame->width = 8;
    frame->height = 8;
    REQUIRE(av_frame_get_buffer(frame, 0) >= 0);
    const std::array<LumaSpot, 1> spot{{{.x = 4, .y = 4, .core = 1, .ringFrom = 2, .ringTo = 3}}};
    CHECK_FALSE(LumaSampler().read(*frame, spot));
    av_frame_free(&frame);
}

TEST_CASE("luma readings: a picture the GPU decoded reads the same as decoded in software")
{
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        MESSAGE("no GPU with video support: skipped");
        return;
    }
    bool hardware = false;
    const auto fromGpu = readingsOf("emulator_h264.media", device.Get(), hardware);
    if (!hardware) {
        MESSAGE("the GPU does not decode H.264: skipped");
        return;
    }
    bool software = true;
    const auto fromSoftware = readingsOf("emulator_h264.media", nullptr, software);
    CHECK_FALSE(software);
    REQUIRE(fromGpu.size() == fromSoftware.size());
    REQUIRE_FALSE(fromGpu.empty());
    for (std::size_t i = 0; i < fromGpu.size(); ++i) {
        CAPTURE(i);
        CHECK(fromGpu[i] == fromSoftware[i]);
    }
}
