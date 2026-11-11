#include "gs/media/FrameSnapshot.h"

#include "gs/media/VideoDecoder.h"

#include <gs/protocol/Constants.h>
#include <gs/vision/Matcher.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <d3d11.h>
#include <wrl/client.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <vector>

using namespace gs;
using gs::media::FrameSink;
using gs::media::FrameSnapshots;
using gs::media::LumaSnapshot;
using gs::media::VideoDecoder;
using gs::media::VideoFrame;
using Microsoft::WRL::ComPtr;

namespace {

#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
constexpr bool kOptimised = true;
#else
constexpr bool kOptimised = false;
#endif

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

FramePtr pictureOf(AVPixelFormat format, int width, int height)
{
    FramePtr frame(av_frame_alloc());
    REQUIRE(frame != nullptr);
    frame->format = format;
    frame->width = width;
    frame->height = height;
    REQUIRE(av_frame_get_buffer(frame.get(), 0) >= 0);
    return frame;
}

FramePtr pictureWithSquare(AVPixelFormat format, int width, int height, int cx, int cy)
{
    FramePtr frame = pictureOf(format, width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool inside = std::abs(x - cx) <= 4 && std::abs(y - cy) <= 4;
            frame->data[0][y * frame->linesize[0] + x] = inside ? 220 : 30;
        }
    }
    return frame;
}

FramePtr noisyPicture(int width, int height)
{
    FramePtr frame = pictureOf(AV_PIX_FMT_YUV420P, width, height);
    std::uint32_t seed = 12345;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            seed = seed * 1664525u + 1013904223u;
            frame->data[0][y * frame->linesize[0] + x] = static_cast<std::uint8_t>(seed >> 24);
        }
    }
    return frame;
}

std::vector<std::uint8_t> readRecording(const char* name)
{
    std::ifstream file(std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / name, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Requester final : FrameSink {
    explicit Requester(FrameSnapshots& target, bool wantColor) : snapshots(target), color(wantColor) {}

    void onFrame(VideoFrame frame) override
    {
        hardware = hardware || frame.hardware();
        snapshots.request(color);
        const auto start = std::chrono::steady_clock::now();
        snapshots.offer(frame);
        offerMs.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }

    FrameSnapshots& snapshots;
    bool color = false;
    std::vector<double> offerMs;
    bool hardware = false;
};

struct Decoded {
    std::vector<std::shared_ptr<const LumaSnapshot>> snapshots;
    std::vector<double> offerMs;
    media::SnapshotCounters counters;
    bool hardware = false;
};

Decoded snapshotsOf(const char* recordingName, ID3D11Device* device, bool color = false)
{
    const auto recording = readRecording(recordingName);
    FrameSnapshots snapshots(device);
    Decoded decoded;
    snapshots.setListener([&](const std::shared_ptr<const LumaSnapshot>& made) { decoded.snapshots.push_back(made); });
    VideoDecoder decoder(device);
    Requester sink(snapshots, color);
    for (std::size_t offset = 0; offset < recording.size();) {
        auto packet =
            protocol::decodeMediaPacket(protocol::MediaChannel::Video, std::span(recording).subspan(offset));
        REQUIRE(packet);
        offset += protocol::kMediaHeaderSize + packet->header.size;
        REQUIRE(decoder.push(*packet, sink));
    }
    snapshots.finish();
    decoded.offerMs = std::move(sink.offerMs);
    decoded.counters = snapshots.counters();
    decoded.hardware = sink.hardware;
    return decoded;
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[std::min(index, values.size() - 1)];
}

FramePtr coloredPicture(AVPixelFormat format, int width, int height)
{
    FramePtr frame = pictureOf(format, width, height);
    frame->colorspace = AVCOL_SPC_SMPTE170M;
    frame->color_range = AVCOL_RANGE_MPEG;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            frame->data[0][y * frame->linesize[0] + x] = x < width / 2 ? 81 : 145;
        }
    }
    for (int y = 0; y < (height + 1) / 2; ++y) {
        for (int x = 0; x < (width + 1) / 2; ++x) {
            const bool left = 2 * x < width / 2;
            const std::uint8_t cb = left ? 90 : 54;
            const std::uint8_t cr = left ? 240 : 34;
            if (format == AV_PIX_FMT_NV12) {
                frame->data[1][y * frame->linesize[1] + 2 * x] = cb;
                frame->data[1][y * frame->linesize[1] + 2 * x + 1] = cr;
            } else {
                frame->data[1][y * frame->linesize[1] + x] = cb;
                frame->data[2][y * frame->linesize[2] + x] = cr;
            }
        }
    }
    return frame;
}

void checkRgb(const std::vector<std::uint8_t>& rgb, std::size_t pixel, int red, int green, int blue)
{
    REQUIRE(rgb.size() >= 3 * pixel + 3);
    CHECK(std::abs(rgb[3 * pixel] - red) <= 2);
    CHECK(std::abs(rgb[3 * pixel + 1] - green) <= 2);
    CHECK(std::abs(rgb[3 * pixel + 2] - blue) <= 2);
}

}

TEST_CASE("frame snapshots: a color request copies the chroma and the matrix too, a luma request still copies luma only")
{
    for (const AVPixelFormat format : {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12}) {
        CAPTURE(static_cast<int>(format));
        const FramePtr picture = coloredPicture(format, 40, 18);
        FrameSnapshots snapshots;
        snapshots.request(true);
        snapshots.offer(*picture, 100, 3);
        const auto colored = snapshots.latest();
        REQUIRE(colored != nullptr);
        REQUIRE(colored->colored());
        CHECK(colored->chromaWidth() == 20);
        CHECK(colored->chromaHeight() == 9);
        REQUIRE(colored->chroma.size() == std::size_t{2} * 20 * 9);
        CHECK(colored->chroma[0] == 90);
        CHECK(colored->chroma[1] == 240);
        CHECK(colored->chroma[2 * 19] == 54);
        CHECK(colored->chroma[2 * 19 + 1] == 34);
        CHECK(colored->luma[0] == 81);
        CHECK(colored->matrix.rows == media::yuvToRgb(*picture).rows);

        const std::vector<std::uint8_t> rgb = media::rgbOf(*colored, 0, 0, 40, 18);
        REQUIRE(rgb.size() == std::size_t{3} * 40 * 18);
        checkRgb(rgb, 0, 255, 0, 0);
        checkRgb(rgb, 39, 0, 255, 0);
        const std::vector<std::uint8_t> corner = media::rgbOf(*colored, 30, 10, 10, 8);
        REQUIRE(corner.size() == std::size_t{3} * 10 * 8);
        checkRgb(corner, 0, 0, 255, 0);
        CHECK(media::rgbOf(*colored, 35, 0, 10, 1).empty());
        CHECK(media::rgbOf(*colored, 0, 0, 0, 1).empty());

        snapshots.request();
        snapshots.offer(*picture, 200, 3);
        const auto plain = snapshots.latest();
        REQUIRE(plain != nullptr);
        CHECK(plain != colored);
        CHECK_FALSE(plain->colored());
        CHECK(plain->luma == colored->luma);
        CHECK(media::rgbOf(*plain, 0, 0, 1, 1).empty());

        snapshots.request();
        snapshots.request(true);
        snapshots.offer(*picture, 300, 3);
        CHECK(snapshots.latest()->colored());

        const auto grey = snapshots.capture();
        REQUIRE(grey != nullptr);
        CHECK_FALSE(grey->colored());
        const auto captured = snapshots.capture(true);
        REQUIRE(captured != nullptr);
        CHECK(captured->chroma == colored->chroma);
        CHECK(captured->matrix.rows == colored->matrix.rows);
        CHECK(snapshots.counters().captured == 2);
    }
}

TEST_CASE("frame snapshots: GPU pictures bring the same chroma back as software decoding (VIS-004)")
{
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        MESSAGE("no GPU with video support: skipped");
        return;
    }
    const Decoded fromGpu = snapshotsOf("emulator_h264.media", device.Get(), true);
    if (!fromGpu.hardware) {
        MESSAGE("the GPU does not decode H.264: skipped");
        return;
    }
    const Decoded fromSoftware = snapshotsOf("emulator_h264.media", nullptr, true);
    REQUIRE_FALSE(fromGpu.snapshots.empty());
    CHECK(fromGpu.counters.failed == 0);
    std::map<std::int64_t, std::shared_ptr<const LumaSnapshot>> software;
    for (const auto& snapshot : fromSoftware.snapshots) {
        REQUIRE(snapshot->colored());
        software.emplace(snapshot->ptsUs, snapshot);
    }
    for (const auto& snapshot : fromGpu.snapshots) {
        CAPTURE(snapshot->ptsUs);
        REQUIRE(snapshot->colored());
        const auto same = software.find(snapshot->ptsUs);
        REQUIRE(same != software.end());
        CHECK(snapshot->luma == same->second->luma);
        CHECK(snapshot->chroma == same->second->chroma);
        CHECK(snapshot->matrix.rows == same->second->matrix.rows);
        CHECK(media::rgbOf(*snapshot, 0, 0, snapshot->width, snapshot->height)
              == media::rgbOf(*same->second, 0, 0, snapshot->width, snapshot->height));
    }
}

TEST_CASE("frame snapshots: nothing is copied until one is asked for, then the next frame's luma, packed")
{
    const FramePtr first = pictureWithSquare(AV_PIX_FMT_YUV420P, 61, 47, 10, 10);
    const FramePtr second = pictureWithSquare(AV_PIX_FMT_YUV420P, 61, 47, 40, 30);
    FrameSnapshots snapshots;
    snapshots.offer(*first, 1000, 7);
    CHECK(snapshots.latest() == nullptr);
    CHECK(snapshots.counters().made == 0);

    snapshots.request();
    snapshots.offer(*second, 2000, 7);
    const auto taken = snapshots.latest();
    REQUIRE(taken != nullptr);
    CHECK(taken->sequence == 1);
    CHECK(taken->ptsUs == 2000);
    CHECK(taken->session == 7);
    CHECK(taken->width == 61);
    CHECK(taken->height == 47);
    REQUIRE(taken->luma.size() == std::size_t{61} * 47);
    CHECK(taken->luma[30 * 61 + 40] == 220);
    CHECK(taken->luma[10 * 61 + 10] == 30);
    CHECK(taken->luma[46 * 61 + 60] == 30);

    snapshots.offer(*first, 3000, 7);
    CHECK(snapshots.latest() == taken);
    CHECK(snapshots.counters().made == 1);

    snapshots.request();
    snapshots.offer(*first, 4000, 7);
    const auto again = snapshots.latest();
    REQUIRE(again != nullptr);
    CHECK(again != taken);
    CHECK(again->sequence == 2);
    CHECK(again->luma[10 * 61 + 10] == 220);
}

TEST_CASE("frame snapshots: NV12 pictures in memory, formats the decoder does not make, and the listener")
{
    const FramePtr nv12 = pictureWithSquare(AV_PIX_FMT_NV12, 64, 36, 20, 20);
    FrameSnapshots snapshots;
    std::vector<std::uint64_t> heard;
    snapshots.setListener([&](const std::shared_ptr<const LumaSnapshot>& made) { heard.push_back(made->sequence); });

    snapshots.request();
    snapshots.offer(*nv12, 10, 1);
    const auto taken = snapshots.latest();
    REQUIRE(taken != nullptr);
    CHECK(taken->luma[20 * 64 + 20] == 220);
    CHECK(taken->luma[0] == 30);

    const FramePtr rgba = pictureOf(AV_PIX_FMT_RGBA, 8, 8);
    snapshots.request();
    snapshots.offer(*rgba, 20, 1);
    CHECK(snapshots.latest() == taken);
    CHECK(snapshots.counters().failed == 1);
    snapshots.offer(*nv12, 30, 1);
    CHECK(snapshots.latest() == taken);

    snapshots.request();
    snapshots.offer(*nv12, 40, 1);
    CHECK(heard == std::vector<std::uint64_t>{1, 2});
    CHECK(snapshots.counters().made == 2);
}

TEST_CASE("ADR-0112: watchers hear of every frame offered, before anything is asked for, until they stop watching")
{
    const FramePtr picture = pictureWithSquare(AV_PIX_FMT_YUV420P, 32, 18, 8, 8);
    FrameSnapshots snapshots;
    int first = 0;
    int second = 0;
    const std::uint64_t one = snapshots.watch([&first] { ++first; });
    const std::uint64_t two = snapshots.watch([&second] { ++second; });
    CHECK(one != two);
    snapshots.offer(*picture, 10, 1);
    snapshots.offer(*picture, 20, 1);
    CHECK(first == 2);
    CHECK(second == 2);
    CHECK(snapshots.counters().made == 0);
    const auto kept = snapshots.capture(true);
    REQUIRE(kept != nullptr);
    CHECK(kept->frame == snapshots.offered());
    CHECK(kept->ptsUs == 20);
    snapshots.unwatch(one);
    snapshots.offer(*picture, 30, 1);
    CHECK(first == 2);
    CHECK(second == 3);
    snapshots.unwatch(two);
    snapshots.unwatch(two);
    snapshots.offer(*picture, 40, 1);
    CHECK(second == 3);
}

TEST_CASE("frame snapshots: one snapshot serves several template searches without another copy")
{
    const FramePtr picture = noisyPicture(320, 240);
    FrameSnapshots snapshots;
    snapshots.request();
    snapshots.offer(*picture, 5, 1);
    const auto taken = snapshots.latest();
    REQUIRE(taken != nullptr);
    const vision::ImageView screen(taken->luma.data(), taken->width, taken->height, taken->width);
    const vision::Image cut = vision::Image::copyOf(screen.crop({150, 90, 40, 40}));
    const auto pattern = vision::Template::make(cut.view());
    REQUIRE(pattern);

    const auto best = vision::findBest(screen, *pattern);
    REQUIRE(best);
    CHECK(best->rect == vision::Rect{150, 90, 40, 40});
    CHECK(best->score > 0.99);
    const auto inArea = vision::findAll(screen, *pattern, {.roi = {100, 50, 120, 100}});
    REQUIRE_FALSE(inArea.empty());
    CHECK(inArea.front().rect == vision::Rect{150, 90, 40, 40});

    snapshots.offer(*picture, 6, 1);
    CHECK(snapshots.latest() == taken);
    CHECK(snapshots.counters().made == 1);
}

TEST_CASE("frame snapshots: GPU pictures come back through the staging ring without waiting, as in software")
{
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        MESSAGE("no GPU with video support: skipped");
        return;
    }
    const Decoded fromGpu = snapshotsOf("emulator_h264.media", device.Get());
    if (!fromGpu.hardware) {
        MESSAGE("the GPU does not decode H.264: skipped");
        return;
    }
    const Decoded fromSoftware = snapshotsOf("emulator_h264.media", nullptr);
    CHECK_FALSE(fromSoftware.hardware);
    REQUIRE_FALSE(fromGpu.snapshots.empty());
    REQUIRE(fromSoftware.snapshots.size() == fromSoftware.offerMs.size());
    CHECK(fromGpu.counters.made == fromGpu.counters.copied);
    CHECK(fromGpu.counters.failed == 0);

    std::map<std::int64_t, std::shared_ptr<const LumaSnapshot>> software;
    for (const auto& snapshot : fromSoftware.snapshots) {
        software.emplace(snapshot->ptsUs, snapshot);
    }
    std::uint64_t previous = 0;
    for (const auto& snapshot : fromGpu.snapshots) {
        CAPTURE(snapshot->ptsUs);
        CHECK(snapshot->sequence > previous);
        previous = snapshot->sequence;
        const auto same = software.find(snapshot->ptsUs);
        REQUIRE(same != software.end());
        CHECK(snapshot->width == same->second->width);
        CHECK(snapshot->height == same->second->height);
        CHECK(snapshot->luma == same->second->luma);
    }

    std::vector<double> steady(fromGpu.offerMs.begin() + std::min<std::ptrdiff_t>(3, std::ssize(fromGpu.offerMs)),
                               fromGpu.offerMs.end());
    const double median = percentile(steady, 0.5);
    const double p95 = percentile(steady, 0.95);
    const double slowest = percentile(steady, 1.0);
    MESSAGE(std::string(kOptimised ? "optimised build: " : "debug build, not checked: ")
            << fromGpu.offerMs.size() << " frames, " << fromGpu.counters.made << " snapshots, "
            << fromGpu.counters.stillDrawing << " still drawing, " << fromGpu.counters.ringFull
            << " ring full; offer() median " << median << " ms, p95 " << p95 << " ms, slowest " << slowest << " ms");
    if (kOptimised) {
        CHECK(p95 < 2.0);
        CHECK(slowest < 16.0);
    }
}
