#include "gs/media/VideoDecoder.h"

#include <gs/protocol/Constants.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

#include <d3d11.h>
#include <wrl/client.h>

#include <doctest/doctest.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>
#include <vector>

using namespace gs;
using gs::media::FrameSink;
using gs::media::VideoDecoder;
using gs::media::VideoDecodeErrorKind;
using gs::media::VideoFrame;
using Microsoft::WRL::ComPtr;

namespace {

std::vector<std::uint8_t> readRecording(std::string_view name)
{
    std::ifstream file(std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / name, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<protocol::MediaPacket> packetsOf(const std::vector<std::uint8_t>& recording)
{
    std::vector<protocol::MediaPacket> packets;
    for (std::size_t offset = 0; offset < recording.size();) {
        auto packet = protocol::decodeMediaPacket(protocol::MediaChannel::Video,
                                                  std::span(recording).subspan(offset));
        REQUIRE(packet);
        offset += protocol::kMediaHeaderSize + packet->header.size;
        packets.push_back(*packet);
    }
    return packets;
}

bool sampleable(const VideoFrame& frame)
{
    auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame.avFrame()->data[0]);
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    return (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;
}

std::uint64_t lumaHash(const VideoFrame& frame)
{
    const AVFrame* picture = frame.avFrame();
    AVFrame* downloaded = nullptr;
    if (frame.hardware()) {
        downloaded = av_frame_alloc();
        REQUIRE(av_hwframe_transfer_data(downloaded, picture, 0) >= 0);
        picture = downloaded;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < frame.height(); ++y) {
        const std::uint8_t* row = picture->data[0] + static_cast<std::ptrdiff_t>(y) * picture->linesize[0];
        for (int x = 0; x < frame.width(); ++x) {
            hash = (hash ^ row[x]) * 1099511628211ULL;
        }
    }
    av_frame_free(&downloaded);
    return hash;
}

struct Recorder final : FrameSink {
    struct Picture {
        std::uint16_t session = 0;
        int width = 0;
        int height = 0;
        bool hardware = false;
        bool sampleable = false;
        std::int64_t ptsUs = 0;
        std::uint64_t luma = 0;
    };

    void onFrame(VideoFrame frame) override
    {
        pictures.push_back({frame.session(), frame.width(), frame.height(), frame.hardware(),
                            frame.hardware() && sampleable(frame), frame.ptsUs(), lumaHash(frame)});
        held.push_back(std::move(frame));
        if (held.size() > 2) {
            held.pop_front();
        }
    }

    std::vector<Picture> pictures;
    std::deque<VideoFrame> held;
};

struct Outcome {
    std::size_t framePackets = 0;
    std::size_t hardwarePictures = 0;
    std::vector<std::uint64_t> luma;
};

Outcome decodeRecording(std::string_view name, ID3D11Device* device)
{
    const auto recording = readRecording(name);
    const auto packets = packetsOf(recording);
    VideoDecoder decoder(device);
    Recorder recorder;
    std::vector<protocol::MediaPacket> frames;
    std::map<std::uint16_t, protocol::VideoSessionInfo> sessions;
    for (const auto& packet : packets) {
        CAPTURE(packet.header.session);
        const auto pushed = decoder.push(packet, recorder);
        REQUIRE_MESSAGE(pushed, (pushed ? std::string() : pushed.error().message));
        if (packet.header.kind == protocol::MediaKind::Session) {
            sessions[packet.header.session] = std::get<protocol::VideoSessionInfo>(packet.session);
        } else if (packet.header.kind == protocol::MediaKind::Frame) {
            frames.push_back(packet);
        }
    }
    REQUIRE(recorder.pictures.size() == frames.size());
    Outcome outcome{frames.size(), 0, {}};
    for (std::size_t i = 0; i < frames.size(); ++i) {
        CAPTURE(i);
        const auto& picture = recorder.pictures[i];
        outcome.luma.push_back(picture.luma);
        const auto& session = sessions.at(frames[i].header.session);
        CHECK(picture.session == frames[i].header.session);
        CHECK(picture.ptsUs == frames[i].header.ptsUs);
        CHECK(picture.width == session.frameWidth);
        CHECK(picture.height == session.frameHeight);
        if (picture.hardware) {
            CHECK(picture.sampleable);
            ++outcome.hardwarePictures;
        }
    }
    return outcome;
}

}

TEST_CASE("video decoder: software decodes every frame, at the size of its session")
{
    for (std::string_view name : {"emulator_h264.media", "emulator_h265.media", "emulator_av1.media"}) {
        CAPTURE(name);
        const Outcome outcome = decodeRecording(name, nullptr);
        CHECK(outcome.framePackets > 40);
        CHECK(outcome.hardwarePictures == 0);
    }
}

TEST_CASE("video decoder: D3D11VA where the GPU decodes the codec, software elsewhere (MED-002)")
{
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        MESSAGE("no hardware D3D11 device: GPU decoding not tested");
        return;
    }
    for (std::string_view name : {"emulator_h264.media", "emulator_h265.media", "emulator_av1.media"}) {
        CAPTURE(name);
        const Outcome outcome = decodeRecording(name, device.Get());
        CHECK((outcome.hardwarePictures == 0 || outcome.hardwarePictures == outcome.framePackets));
        MESSAGE((std::string(name) + (outcome.hardwarePictures == 0 ? ": software" : ": GPU")));
        CHECK(outcome.luma == decodeRecording(name, nullptr).luma);
    }
}

TEST_CASE("video decoder: a renderer on WARP, as without a GPU (R12), gets software pictures")
{
    ComPtr<ID3D11Device> warp;
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                        &warp, nullptr, nullptr)));
    const Outcome outcome = decodeRecording("emulator_h264.media", warp.Get());
    CHECK(outcome.hardwarePictures == 0);
    CHECK(outcome.luma == decodeRecording("emulator_h264.media", nullptr).luma);
}

TEST_CASE("video decoder: packets out of order are reported")
{
    const auto recording = readRecording("emulator_h264.media");
    const auto packets = packetsOf(recording);
    REQUIRE(packets.size() > 3);
    REQUIRE(packets[0].header.kind == protocol::MediaKind::Session);
    REQUIRE(packets[1].header.kind == protocol::MediaKind::Config);
    REQUIRE(packets[2].header.kind == protocol::MediaKind::Frame);
    Recorder recorder;

    VideoDecoder decoder;
    auto pushed = decoder.push(packets[2], recorder);
    REQUIRE_FALSE(pushed);
    CHECK(pushed.error().kind == VideoDecodeErrorKind::OutOfOrder);
    pushed = decoder.push(packets[1], recorder);
    REQUIRE_FALSE(pushed);
    CHECK(pushed.error().kind == VideoDecodeErrorKind::OutOfOrder);

    REQUIRE(decoder.push(packets[0], recorder));
    REQUIRE(decoder.push(packets[1], recorder));
    auto stray = packets[2];
    stray.header.session = static_cast<std::uint16_t>(stray.header.session + 1);
    pushed = decoder.push(stray, recorder);
    REQUIRE_FALSE(pushed);
    CHECK(pushed.error().kind == VideoDecodeErrorKind::OutOfOrder);
    CHECK(decoder.push(packets[2], recorder));
    CHECK(recorder.pictures.size() == 1);

    protocol::MediaPacket audio = packets[0];
    audio.session = protocol::AudioSessionInfo{};
    pushed = decoder.push(audio, recorder);
    REQUIRE_FALSE(pushed);
    CHECK(pushed.error().kind == VideoDecodeErrorKind::OutOfOrder);
}
