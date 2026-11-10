#include "CaptureFeed.h"

#include <gs/capture/CaptureFormat.h>
#include <gs/capture/CaptureSource.h>
#include <gs/capture/CropDetector.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/media/VideoDecoder.h>
#include <gs/record/RecordedAudio.h>
#include <gs/record/Recorder.h>
#include <gs/record/Recording.h>
#include <gs/render/FrameSlot.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <windows.h>

#include <objbase.h>

#include <doctest/doctest.h>

#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace gs;
using gs::app::CaptureEvent;
using gs::app::CaptureFeed;
using gs::app::CaptureOptions;
using gs::media::VideoFrame;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

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

class Folder
{
public:
    Folder()
        : m_root(fs::temp_directory_path() /
                 ("gs_capture_" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}())))
    {
        fs::create_directories(m_root);
    }

    ~Folder()
    {
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
    }

    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;

    [[nodiscard]] const fs::path& path() const noexcept { return m_root; }

private:
    fs::path m_root;
};

class FakeSource final : public capture::CaptureSource
{
public:
    explicit FakeSource(std::vector<capture::CaptureFormat> formats, std::string refusal = {})
        : m_formats(std::move(formats)), m_refusal(std::move(refusal))
    {
    }

    [[nodiscard]] bool start(capture::SampleSink& sink, const std::string& format) override
    {
        ++starts;
        asked = format;
        if (!m_refusal.empty() || refuseQuietly) {
            return false;
        }
        m_sink = &sink;
        sink.offered(m_formats, capture::chooseFormat(m_formats, format));
        return true;
    }

    void stop() override { m_sink = nullptr; }
    [[nodiscard]] std::string problem() const override { return m_refusal; }

    void deliver(const capture::CaptureSample& sample) const
    {
        if (m_sink != nullptr) {
            m_sink->sample(sample);
        }
    }

    void end(const std::string& problem) const
    {
        if (m_sink != nullptr) {
            m_sink->ended(problem);
        }
    }

    int starts = 0;
    std::string asked;
    bool refuseQuietly = false;

private:
    std::vector<capture::CaptureFormat> m_formats;
    std::string m_refusal;
    capture::SampleSink* m_sink = nullptr;
};

class Events
{
public:
    CaptureFeed::Listener listener()
    {
        return [this](CaptureEvent event) {
            const std::scoped_lock lock(m_mutex);
            m_heard.push_back(event);
        };
    }

    [[nodiscard]] int count(CaptureEvent event) const
    {
        const std::scoped_lock lock(m_mutex);
        return static_cast<int>(std::ranges::count(m_heard, event));
    }

private:
    mutable std::mutex m_mutex;
    std::vector<CaptureEvent> m_heard;
};

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr int kPhoneLeft = 118;
constexpr int kPhoneWidth = 84;

std::vector<capture::CaptureFormat> cardFormats()
{
    return {{.width = kWidth, .height = kHeight, .rateNumerator = 60, .subtype = capture::Subtype::Nv12},
            {.width = kWidth, .height = kHeight, .rateNumerator = 30, .subtype = capture::Subtype::Yuy2},
            {.width = 32, .height = 16, .rateNumerator = 30, .subtype = capture::Subtype::Mjpg},
            {.width = 640, .height = 480, .rateNumerator = 30, .subtype = capture::Subtype::Other}};
}

struct Picture {
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] std::uint8_t luma(int x, int y) const { return bytes[static_cast<std::size_t>(y * kWidth + x)]; }
};

Picture phoneInBars()
{
    Picture picture{std::vector<std::uint8_t>(static_cast<std::size_t>(kWidth * kHeight * 3 / 2), 128)};
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const bool phone = x >= kPhoneLeft && x < kPhoneLeft + kPhoneWidth;
            picture.bytes[static_cast<std::size_t>(y * kWidth + x)] =
                phone ? static_cast<std::uint8_t>(40 + (x * 7 + y * 13) % 180) : 16;
        }
    }
    return picture;
}

std::int64_t nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

capture::CaptureSample nv12Sample(const Picture& picture)
{
    const std::int64_t at = nowUs();
    return {.kind = capture::SampleKind::Nv12,
            .width = kWidth,
            .height = kHeight,
            .bytes = picture.bytes,
            .pitch = kWidth,
            .chromaOffset = kWidth * kHeight,
            .ptsUs = at,
            .receivedUs = at};
}

template <typename Done>
bool feedUntil(const FakeSource& source, const Picture& picture, std::chrono::milliseconds most, const Done& done)
{
    const auto deadline = std::chrono::steady_clock::now() + most;
    while (std::chrono::steady_clock::now() < deadline) {
        source.deliver(nv12Sample(picture));
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return done();
}

std::vector<std::uint8_t> testJpeg()
{
    std::ifstream file(fs::path(GS_CAPTURE_TEST_DATA_DIR) / "halves-32x16.jpg.hex");
    REQUIRE(file);
    std::vector<std::uint8_t> bytes;
    std::string token;
    while (file >> token) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)));
    }
    return bytes;
}

}

TEST_CASE("a capture feed shows the card's NV12 pictures and finds the phone screen between the black bars (MED-014)")
{
    auto owned = std::make_unique<FakeSource>(cardFormats());
    FakeSource& source = *owned;
    CaptureFeed feed(nullptr, std::move(owned));
    Events events;
    feed.setListener(events.listener());
    REQUIRE(feed.start(CaptureOptions{.sound = false}));
    CHECK(feed.running());
    CHECK(source.starts == 1);
    CHECK(source.asked.empty());
    CHECK(events.count(CaptureEvent::Opened) == 1);
    CHECK(feed.formatText() == QStringLiteral("320 × 180 · 60 fps · NV12"));
    CHECK(feed.formats() ==
          QStringList{QStringLiteral("320x180@60/NV12"), QStringLiteral("320x180@30/YUY2"), QStringLiteral("32x16@30/MJPG")});
    CHECK(feed.crop().whole());
    CHECK_FALSE(feed.manualCrop());
    CHECK_FALSE(feed.sound());

    const Picture picture = phoneInBars();
    source.deliver(nv12Sample(picture));
    CHECK(feed.frameSize() == QSize(kWidth, kHeight));
    CHECK(feed.cropSize() == QSize(kWidth, kHeight));
    CHECK(events.count(CaptureEvent::Cropped) == 1);
    const std::uint16_t before = feed.touchSession();
    const VideoFrame shown = feed.frames()->take();
    REQUIRE(shown);
    CHECK(shown.width() == kWidth);
    CHECK(shown.height() == kHeight);

    REQUIRE(feedUntil(source, picture, 5s, [&feed] { return !feed.crop().whole(); }));
    const capture::CropBox expected{.x = static_cast<double>(kPhoneLeft) / kWidth,
                                    .y = 0.0,
                                    .width = static_cast<double>(kPhoneWidth) / kWidth,
                                    .height = 1.0};
    CHECK(feed.crop().closeTo(expected, 1e-9));
    CHECK(feed.cropSize() == QSize(kPhoneWidth, kHeight));
    CHECK(feed.frameSize() == QSize(kWidth, kHeight));
    CHECK(events.count(CaptureEvent::Cropped) == 2);
    CHECK(feed.touchSession() != before);
    CHECK_FALSE(feed.manualCrop());

    feed.snapshots()->request();
    source.deliver(nv12Sample(picture));
    const auto shot = feed.snapshots()->latest();
    REQUIRE(shot != nullptr);
    CHECK(shot->width == kPhoneWidth);
    CHECK(shot->height == kHeight);
    CHECK(shot->luma[0] == picture.luma(kPhoneLeft, 0));
    CHECK(shot->luma[static_cast<std::size_t>(100 * kPhoneWidth + 50)] == picture.luma(kPhoneLeft + 50, 100));
    CHECK(feed.framesShown() > 3);

    feed.stop();
    CHECK_FALSE(feed.running());
    const std::uint64_t shownBefore = feed.framesShown();
    source.deliver(nv12Sample(picture));
    CHECK(feed.framesShown() == shownBefore);
}

TEST_CASE("a capture feed turns YUY2 and MJPEG pictures from the card into frames (MED-014)")
{
    const Apartment apartment;
    auto owned = std::make_unique<FakeSource>(cardFormats());
    FakeSource& source = *owned;
    CaptureFeed feed(nullptr, std::move(owned));

    REQUIRE(feed.start(CaptureOptions{.format = "320x180@30/YUY2", .sound = false}));
    CHECK(source.asked == "320x180@30/YUY2");
    CHECK(feed.formatText() == QStringLiteral("320 × 180 · 30 fps · YUY2"));
    std::vector<std::uint8_t> packed(static_cast<std::size_t>(2 * kWidth * kHeight));
    for (int y = 0; y < kHeight; ++y) {
        for (int pair = 0; pair < kWidth / 2; ++pair) {
            const auto at = static_cast<std::size_t>(y * 2 * kWidth + 4 * pair);
            packed[at] = static_cast<std::uint8_t>(16 + (2 * pair + y) % 200);
            packed[at + 1] = 100;
            packed[at + 2] = static_cast<std::uint8_t>(16 + (2 * pair + 1 + y) % 200);
            packed[at + 3] = 150;
        }
    }
    source.deliver({.kind = capture::SampleKind::Yuy2, .width = kWidth, .height = kHeight, .bytes = packed,
                    .pitch = 2 * kWidth, .ptsUs = 10});
    const VideoFrame converted = feed.frames()->take();
    REQUIRE(converted);
    CHECK(converted.width() == kWidth);
    const AVFrame& raw = *converted.avFrame();
    CHECK(raw.data[0][0] == 16);
    CHECK(raw.data[0][5 * raw.linesize[0] + 7] == static_cast<std::uint8_t>(16 + 12));
    CHECK(raw.data[1][0] == 100);
    CHECK(raw.data[1][1] == 150);

    REQUIRE(feed.start(CaptureOptions{.format = "32x16@30/MJPG", .sound = false}));
    CHECK(feed.formatText() == QStringLiteral("32 × 16 · 30 fps · MJPG"));
    const std::vector<std::uint8_t> jpeg = testJpeg();
    source.deliver({.kind = capture::SampleKind::Mjpg, .width = 32, .height = 16, .bytes = jpeg, .ptsUs = 20});
    const VideoFrame decoded = feed.frames()->take();
    if (!decoded) {
        MESSAGE("no MJPEG decoder on this system (" << feed.problem().toStdString() << "): skipped");
        CHECK_FALSE(feed.problem().isEmpty());
        return;
    }
    CHECK(decoded.width() == 32);
    CHECK(decoded.height() == 16);
    feed.snapshots()->request();
    source.deliver({.kind = capture::SampleKind::Mjpg, .width = 32, .height = 16, .bytes = jpeg, .ptsUs = 30});
    const auto shot = feed.snapshots()->latest();
    REQUIRE(shot != nullptr);
    CHECK(std::abs(shot->luma[static_cast<std::size_t>(4 * 32 + 4)] - 192) <= 3);
    CHECK(std::abs(shot->luma[static_cast<std::size_t>(12 * 32 + 28)] - 64) <= 3);
}

TEST_CASE("a picture area picked by hand wins over the automatic one until it is taken back (MED-014)")
{
    auto owned = std::make_unique<FakeSource>(cardFormats());
    FakeSource& source = *owned;
    CaptureFeed feed(nullptr, std::move(owned));
    Events events;
    feed.setListener(events.listener());
    const capture::CropBox manual{.x = 0.25, .y = 0.0, .width = 0.5, .height = 1.0};
    REQUIRE(feed.start(CaptureOptions{.sound = false, .manualCrop = manual}));
    CHECK(feed.manualCrop());
    CHECK(feed.crop() == manual);

    const Picture picture = phoneInBars();
    source.deliver(nv12Sample(picture));
    CHECK(feed.cropSize() == QSize(160, kHeight));
    feed.snapshots()->request();
    source.deliver(nv12Sample(picture));
    const auto shot = feed.snapshots()->latest();
    REQUIRE(shot != nullptr);
    CHECK(shot->width == 160);

    const int cropped = events.count(CaptureEvent::Cropped);
    const std::uint16_t session = feed.touchSession();
    feed.setManualCrop(manual);
    CHECK(events.count(CaptureEvent::Cropped) == cropped);
    CHECK(feed.touchSession() == session);

    const capture::CropBox moved{.x = 0.3, .y = 0.1, .width = 0.4, .height = 0.8};
    feed.setManualCrop(moved);
    CHECK(feed.crop() == moved);
    CHECK(events.count(CaptureEvent::Cropped) == cropped + 1);
    CHECK(feed.touchSession() != session);

    feed.setManualCrop(std::nullopt);
    CHECK_FALSE(feed.manualCrop());
    CHECK(feed.crop().whole());
    CHECK(events.count(CaptureEvent::Cropped) == cropped + 2);
    feed.snapshots()->request();
    source.deliver(nv12Sample(picture));
    const auto whole = feed.snapshots()->latest();
    REQUIRE(whole != nullptr);
    CHECK(whole->width == kWidth);
}

TEST_CASE("a capture card that cannot start or that stops says why (MED-014)")
{
    {
        CaptureFeed feed(nullptr, std::make_unique<FakeSource>(cardFormats(), "the card is busy"));
        CHECK_FALSE(feed.start(CaptureOptions{.sound = false}));
        CHECK_FALSE(feed.running());
        CHECK(feed.problem() == QStringLiteral("the card is busy"));
    }
    {
        auto owned = std::make_unique<FakeSource>(cardFormats());
        owned->refuseQuietly = true;
        CaptureFeed feed(nullptr, std::move(owned));
        CHECK_FALSE(feed.start(CaptureOptions{.sound = false}));
        CHECK(feed.problem() == QStringLiteral("The capture device could not be started"));
    }
    {
        CaptureFeed feed(nullptr, nullptr);
        CHECK_FALSE(feed.start(CaptureOptions{.sound = false}));
        CHECK_FALSE(feed.problem().isEmpty());
    }
    auto owned = std::make_unique<FakeSource>(cardFormats());
    FakeSource& source = *owned;
    CaptureFeed feed(nullptr, std::move(owned));
    Events events;
    feed.setListener(events.listener());
    REQUIRE(feed.start(CaptureOptions{.sound = false}));
    CHECK(feed.problem().isEmpty());
    source.end("the capture card was unplugged");
    CHECK(events.count(CaptureEvent::Failed) == 1);
    CHECK(feed.problem() == QStringLiteral("the capture card was unplugged"));
    REQUIRE(feed.start(CaptureOptions{.sound = false}));
    CHECK(feed.problem().isEmpty());
    CHECK(source.starts == 2);
}

TEST_CASE("ADR-0107: a capture feed makes a small picture for the device card while it shows the card's picture")
{
    auto owned = std::make_unique<FakeSource>(cardFormats());
    FakeSource& source = *owned;
    CaptureFeed feed(nullptr, std::move(owned));
    Events events;
    feed.setListener(events.listener());
    REQUIRE(feed.start(CaptureOptions{.sound = false}));
    CHECK_FALSE(feed.takeThumbnail().has_value());

    const Picture picture = phoneInBars();
    REQUIRE(feedUntil(source, picture, 5s, [&events] { return events.count(CaptureEvent::Thumbnailed) > 0; }));
    const std::optional<media::Thumbnail> made = feed.takeThumbnail();
    REQUIRE(made.has_value());
    CHECK(std::max(made->width, made->height) == 160);
    CHECK(made->width > made->height);
    CHECK(made->pixels.size() == static_cast<std::size_t>(made->width) * static_cast<std::size_t>(made->height));
    CHECK_FALSE(feed.takeThumbnail().has_value());
    feed.stop();
}

TEST_CASE("a capture card's recording holds the cropped picture as H.264 and its sound as AAC (MED-014)")
{
    const Apartment apartment;
    auto owned = std::make_unique<FakeSource>(cardFormats());
    FakeSource& source = *owned;
    CaptureFeed feed(nullptr, std::move(owned));
    const capture::CropBox manual{.x = kPhoneLeft / static_cast<double>(kWidth),
                                  .y = 0.0,
                                  .width = kPhoneWidth / static_cast<double>(kWidth),
                                  .height = 1.0};
    REQUIRE(feed.start(CaptureOptions{.sound = false, .manualCrop = manual}));

    const Folder root;
    auto started = record::Recorder::start(record::RecorderOptions{.folder = root.path() / "card.gsrec"});
    REQUIRE(started);
    const std::shared_ptr<record::Recorder> recorder = *started;
    feed.setRecorder(recorder);

    const Picture picture = phoneInBars();
    std::vector<float> chunk(480 * 2);
    const std::int64_t soundStart = nowUs();
    for (int block = 0; block < 60; ++block) {
        for (int index = 0; index < 480; ++index) {
            const double phase = 2.0 * std::numbers::pi * 440.0 * (block * 480 + index) / 48'000.0;
            chunk[static_cast<std::size_t>(2 * index)] = static_cast<float>(0.5 * std::sin(phase));
            chunk[static_cast<std::size_t>(2 * index + 1)] = static_cast<float>(0.5 * std::sin(phase));
        }
        feed.feedSound(chunk, 2, 48'000, soundStart + std::int64_t{block} * 10'000);
        if (block % 2 == 0) {
            source.deliver(nv12Sample(picture));
        }
        std::this_thread::sleep_for(10ms);
    }
    feed.setRecorder(nullptr);
    const record::RecorderSummary summary = recorder->stop();
    CHECK(summary.problem.empty());

    if (summary.video) {
        CHECK(summary.width == 84);
        CHECK(summary.height == kHeight);
    } else {
        MESSAGE("no H.264 encoder on this system: the picture of the recording is not checked");
    }

    if (!summary.audio) {
        MESSAGE("no AAC encoder on this system: the sound of the recording is not checked");
        return;
    }
    const auto sound = record::decodeRecordedSound(root.path() / "card.gsrec" / record::kAudioFile);
    REQUIRE_MESSAGE(sound, (sound ? std::string() : sound.error()));
    CHECK(sound->aac);
    CHECK(sound->sampleRate == 48'000);
    CHECK(sound->channels == 2);
    CHECK(sound->packets > 10);
    CHECK(sound->firstPtsUs >= soundStart - 100'000);
    REQUIRE(sound->samples.size() > 2 * 12'000);
    double energy = 0.0;
    for (const float sample : sound->samples) {
        energy += static_cast<double>(sample) * sample;
    }
    CHECK(std::sqrt(energy / static_cast<double>(sound->samples.size())) > 0.15);
}
