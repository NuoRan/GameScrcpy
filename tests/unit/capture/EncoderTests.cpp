#include <gs/capture/Encoders.h>

#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Codec.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <windows.h>

#include <objbase.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

using gs::capture::AudioEncoder;
using gs::capture::EncodedSound;
using gs::capture::PixelBox;
using gs::capture::VideoEncoder;
using gs::media::VideoFrame;
using gs::protocol::MediaKind;
using gs::protocol::MediaPacket;
using namespace std::chrono_literals;

namespace {

class Apartment
{
public:
    Apartment() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~Apartment()
    {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;

private:
    HRESULT m_result;
};

struct Kept {
    gs::protocol::MediaHeader header;
    std::variant<std::monostate, gs::protocol::VideoSessionInfo, gs::protocol::AudioSessionInfo> session;
    std::vector<std::uint8_t> payload;
};

class Packets
{
public:
    void add(const MediaPacket& packet)
    {
        {
            const std::scoped_lock lock(m_mutex);
            Kept kept{.header = packet.header, .payload = {packet.payload.begin(), packet.payload.end()}};
            if (const auto* video = std::get_if<gs::protocol::VideoSessionInfo>(&packet.session)) {
                kept.session = *video;
            }
            m_kept.push_back(std::move(kept));
        }
        m_added.notify_all();
    }

    bool waitFor(std::size_t count, std::chrono::milliseconds most)
    {
        std::unique_lock lock(m_mutex);
        return m_added.wait_for(lock, most, [&] { return m_kept.size() >= count; });
    }

    std::vector<Kept> taken()
    {
        const std::scoped_lock lock(m_mutex);
        return m_kept;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_added;
    std::vector<Kept> m_kept;
};

struct Collector final : gs::media::FrameSink {
    void onFrame(VideoFrame frame) override { frames.push_back(std::move(frame)); }
    std::vector<VideoFrame> frames;
};

VideoFrame halves(int width, int height, std::int64_t ptsUs)
{
    std::vector<std::uint8_t> luma(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::vector<std::uint8_t> chroma(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) / 2, 128);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            luma[static_cast<std::size_t>(y * width + x)] = x < width / 2 ? 180 : 60;
        }
    }
    return VideoFrame::copyNv12(luma.data(), width, chroma.data(), width, width, height, ptsUs, 1, ptsUs);
}

double averageLuma(const AVFrame& frame, int left, int width)
{
    double sum = 0.0;
    for (int y = 0; y < frame.height; ++y) {
        for (int x = left; x < left + width; ++x) {
            sum += frame.data[0][y * frame.linesize[0] + x];
        }
    }
    return sum / (static_cast<double>(width) * frame.height);
}

}

TEST_CASE("the H.264 parameter sets are taken from the first key frame, each behind a four-byte start code")
{
    const std::vector<std::uint8_t> unit{0, 0, 0, 1, 0x67, 0x42, 0xC0, 0x1F, 0, 0, 1, 0x68, 0xCE, 0x3C, 0x80,
                                         0, 0, 1, 0x06, 0x05, 0xFF, 0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00};
    const std::vector<std::uint8_t> expected{0, 0, 0, 1, 0x67, 0x42, 0xC0, 0x1F, 0, 0, 0, 1, 0x68, 0xCE, 0x3C, 0x80};
    CHECK(gs::capture::h264ParameterSets(unit) == expected);
    CHECK(gs::capture::h264ParameterSets(std::vector<std::uint8_t>{0, 0, 1, 0x65, 0x88}).empty());
    CHECK(gs::capture::h264ParameterSets(std::vector<std::uint8_t>{}).empty());
    CHECK(gs::capture::h264ParameterSets(std::vector<std::uint8_t>{0x67, 0x42, 0x00}).empty());
}

TEST_CASE("the AAC configuration names AAC-LC, the sample rate and the channels")
{
    CHECK(gs::capture::aacConfig(48'000, 2) == std::vector<std::uint8_t>{0x11, 0x90});
    CHECK(gs::capture::aacConfig(44'100, 1) == std::vector<std::uint8_t>{0x12, 0x08});
    CHECK(gs::capture::aacConfig(12'345, 2) == std::vector<std::uint8_t>{0x11, 0x90});
}

TEST_CASE("the recording encoder turns the cropped part of the pictures into H.264 that decodes to the same picture")
{
    Packets packets;
    {
        VideoEncoder encoder(nullptr, [&packets](const MediaPacket& packet) { packets.add(packet); });
        const PixelBox region{64, 0, 128, 128};
        encoder.encode(halves(256, 128, 1'000'000), region, 1'000'000);
        if (!packets.waitFor(1, 10s)) {
            MESSAGE("no H.264 encoder on this system (" << encoder.problem() << "): skipped");
            return;
        }
        for (int index = 1; index < 12; ++index) {
            const std::int64_t at = 1'000'000 + std::int64_t{index} * 16'667;
            encoder.encode(halves(256, 128, at), region, at);
            static_cast<void>(packets.waitFor(static_cast<std::size_t>(index) + 2, 500ms));
        }
        CHECK(encoder.problem().empty());
    }
    const std::vector<Kept> kept = packets.taken();
    REQUIRE(kept.size() >= 3);
    REQUIRE(kept[0].header.kind == MediaKind::Session);
    const auto* info = std::get_if<gs::protocol::VideoSessionInfo>(&kept[0].session);
    REQUIRE(info != nullptr);
    CHECK(info->codec == gs::protocol::VideoCodec::H264);
    CHECK(info->frameWidth == 128);
    CHECK(info->frameHeight == 128);
    CHECK(kept[1].header.kind == MediaKind::Config);
    CHECK(kept[2].header.kind == MediaKind::Frame);
    CHECK(kept[2].header.keyFrame);
    CHECK(kept[2].header.ptsUs == 1'000'000);

    gs::media::VideoDecoder decoder(nullptr);
    Collector collector;
    for (const Kept& each : kept) {
        MediaPacket packet{.header = each.header, .session = std::monostate{}, .payload = each.payload};
        if (const auto* video = std::get_if<gs::protocol::VideoSessionInfo>(&each.session)) {
            packet.session = *video;
        }
        REQUIRE(decoder.push(packet, collector));
    }
    REQUIRE_FALSE(collector.frames.empty());
    const AVFrame& picture = *collector.frames.front().avFrame();
    CHECK(picture.width == 128);
    CHECK(picture.height == 128);
    CHECK(averageLuma(picture, 8, 48) == doctest::Approx(180.0).epsilon(0.08));
    CHECK(averageLuma(picture, 72, 48) == doctest::Approx(60.0).epsilon(0.15));
}

TEST_CASE("the recording sound encoder gives the AAC configuration first, then AAC frames with growing times")
{
    const Apartment apartment;
    std::vector<EncodedSound> heard;
    std::vector<std::vector<std::uint8_t>> bytes;
    AudioEncoder encoder([&](const EncodedSound& sound) {
        heard.push_back(sound);
        bytes.emplace_back(sound.bytes.begin(), sound.bytes.end());
    });
    std::vector<float> chunk(480 * 2);
    for (int block = 0; block < 50; ++block) {
        for (int index = 0; index < 480; ++index) {
            const double phase = 2.0 * std::numbers::pi * 440.0 * (block * 480 + index) / 48'000.0;
            chunk[static_cast<std::size_t>(2 * index)] = static_cast<float>(0.5 * std::sin(phase));
            chunk[static_cast<std::size_t>(2 * index + 1)] = static_cast<float>(0.5 * std::sin(phase));
        }
        encoder.encode(chunk, 2, 48'000, 5'000'000 + std::int64_t{block} * 10'000);
    }
    if (heard.empty()) {
        MESSAGE("no AAC encoder on this system (" << encoder.problem() << "): skipped");
        CHECK_FALSE(encoder.problem().empty());
        return;
    }
    REQUIRE(heard.front().config);
    CHECK(bytes.front() == std::vector<std::uint8_t>{0x11, 0x90});
    CHECK(heard.front().sampleRate == 48'000);
    CHECK(heard.front().channels == 2);
    CHECK(heard.front().ptsUs == 5'000'000);
    REQUIRE(heard.size() > 10);
    std::int64_t previous = 0;
    for (std::size_t index = 1; index < heard.size(); ++index) {
        CAPTURE(index);
        CHECK_FALSE(heard[index].config);
        CHECK_FALSE(bytes[index].empty());
        CHECK(heard[index].ptsUs >= 4'900'000);
        CHECK(heard[index].ptsUs >= previous);
        previous = heard[index].ptsUs;
    }
    CHECK(encoder.problem().empty());
}
