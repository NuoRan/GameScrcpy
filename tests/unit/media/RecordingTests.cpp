#include "stability/ChildProcess.h"

#include <gs/media/FrameSnapshot.h>
#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Codec.h>
#include <gs/protocol/Constants.h>
#include <gs/record/MediaFile.h>
#include <gs/record/Mp4Export.h>
#include <gs/record/Operations.h>
#include <gs/record/RecordedVision.h>
#include <gs/record/Recorder.h>
#include <gs/record/Recording.h>
#include <gs/script/Replay.h>
#include <gs/script/Session.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

using namespace std::chrono_literals;
using gs::ManualClock;
using gs::Microseconds;
using gs::protocol::MediaChannel;
using gs::protocol::MediaKind;
using gs::protocol::MediaPacket;
namespace record = gs::record;
namespace script = gs::script;

namespace {

namespace fs = std::filesystem;

class Folder
{
public:
    Folder()
        : m_root(fs::temp_directory_path() /
                 ("gs_recording_" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}())))
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

fs::path media(std::string_view name)
{
    return fs::path(GS_VIDEO_TEST_DATA_DIR) / name;
}

std::vector<std::uint8_t> readBytes(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& path, std::span<const std::uint8_t> bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeText(const fs::path& path, std::string_view text)
{
    writeBytes(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::vector<MediaPacket> packetsOf(std::span<const std::uint8_t> bytes, MediaChannel channel = MediaChannel::Video)
{
    std::vector<MediaPacket> packets;
    for (std::size_t offset = 0; offset < bytes.size();) {
        const auto packet = gs::protocol::decodeMediaPacket(channel, bytes.subspan(offset));
        REQUIRE(packet);
        offset += gs::protocol::kMediaHeaderSize + packet->header.size;
        packets.push_back(*packet);
    }
    return packets;
}

struct Decoded {
    std::int64_t ptsUs = 0;
    gs::media::VideoFrame frame;
};

struct Collector final : gs::media::FrameSink {
    void onFrame(gs::media::VideoFrame frame) override
    {
        const std::int64_t pts = frame.ptsUs();
        frames.push_back(Decoded{.ptsUs = pts, .frame = std::move(frame)});
    }

    std::vector<Decoded> frames;
};

std::vector<Decoded> decodeAll(const std::vector<MediaPacket>& packets)
{
    gs::media::VideoDecoder decoder;
    Collector collector;
    for (const MediaPacket& packet : packets) {
        REQUIRE(decoder.push(packet, collector));
    }
    return std::move(collector.frames);
}

std::uint32_t big32(std::span<const std::uint8_t> bytes, std::size_t at)
{
    return (std::uint32_t{bytes[at]} << 24U) | (std::uint32_t{bytes[at + 1]} << 16U)
           | (std::uint32_t{bytes[at + 2]} << 8U) | std::uint32_t{bytes[at + 3]};
}

std::uint64_t big64(std::span<const std::uint8_t> bytes, std::size_t at)
{
    return (std::uint64_t{big32(bytes, at)} << 32U) | big32(bytes, at + 4);
}

struct Box {
    std::string type;
    std::span<const std::uint8_t> body;
};

std::vector<Box> boxesIn(std::span<const std::uint8_t> data)
{
    std::vector<Box> boxes;
    for (std::size_t at = 0; at + 8 <= data.size();) {
        std::uint64_t size = big32(data, at);
        std::size_t header = 8;
        if (size == 1 && at + 16 <= data.size()) {
            size = big64(data, at + 8);
            header = 16;
        } else if (size == 0) {
            size = data.size() - at;
        }
        if (size < header || at + size > data.size()) {
            break;
        }
        boxes.push_back(Box{.type = std::string(reinterpret_cast<const char*>(data.data() + at + 4), 4),
                            .body = data.subspan(at + header, static_cast<std::size_t>(size) - header)});
        at += static_cast<std::size_t>(size);
    }
    return boxes;
}

std::optional<std::span<const std::uint8_t>> child(std::span<const std::uint8_t> data, std::string_view type)
{
    for (const Box& box : boxesIn(data)) {
        if (box.type == type) {
            return box.body;
        }
    }
    return std::nullopt;
}

struct Track {
    std::string handler;
    double seconds = 0.0;
    double width = 0.0;
    double height = 0.0;
    std::uint32_t samples = 0;
};

struct Movie {
    double seconds = 0.0;
    std::vector<Track> tracks;
};

double durationOf(std::span<const std::uint8_t> header, std::size_t versionOneOffset, std::size_t versionZeroOffset)
{
    if (header[0] == 1) {
        return static_cast<double>(big64(header, versionOneOffset + 4)) / big32(header, versionOneOffset);
    }
    return static_cast<double>(big32(header, versionZeroOffset + 4)) / big32(header, versionZeroOffset);
}

Movie movieOf(const fs::path& file)
{
    const std::vector<std::uint8_t> bytes = readBytes(file);
    const std::vector<Box> top = boxesIn(bytes);
    REQUIRE(std::ranges::any_of(top, [](const Box& box) { return box.type == "ftyp"; }));
    REQUIRE(std::ranges::any_of(top, [](const Box& box) { return box.type == "mdat"; }));
    const auto moov = child(bytes, "moov");
    REQUIRE(moov);
    const auto mvhd = child(*moov, "mvhd");
    REQUIRE(mvhd);
    Movie movie{.seconds = durationOf(*mvhd, 20, 12)};
    for (const Box& box : boxesIn(*moov)) {
        if (box.type != "trak") {
            continue;
        }
        Track track;
        const auto tkhd = child(box.body, "tkhd");
        const auto mdia = child(box.body, "mdia");
        REQUIRE(tkhd);
        REQUIRE(mdia);
        const std::size_t sizeAt = (*tkhd)[0] == 1 ? 88 : 76;
        track.width = big32(*tkhd, sizeAt) / 65536.0;
        track.height = big32(*tkhd, sizeAt + 4) / 65536.0;
        const auto mdhd = child(*mdia, "mdhd");
        const auto hdlr = child(*mdia, "hdlr");
        const auto minf = child(*mdia, "minf");
        REQUIRE(mdhd);
        REQUIRE(hdlr);
        REQUIRE(minf);
        track.seconds = durationOf(*mdhd, 20, 12);
        track.handler = std::string(reinterpret_cast<const char*>(hdlr->data() + 8), 4);
        const auto stbl = child(*minf, "stbl");
        REQUIRE(stbl);
        const auto stsz = child(*stbl, "stsz");
        REQUIRE(stsz);
        track.samples = big32(*stsz, 8);
        movie.tracks.push_back(track);
    }
    return movie;
}

std::string sessionText(std::int64_t startPts, std::int64_t durationMs, std::string_view extra = {})
{
    return std::format(R"({{"format": "gamescrcpy.session", "version": 2,
        "device": {{"model": "Emulator", "android": 15, "width": 288, "height": 640}},
        "startedAt": "2026-11-04T12:00:00Z", "frameRate": 30, "video": "video.media",
        "videoStartPtsUs": {}, "durationMs": {}{}}})",
                       startPts, durationMs, extra);
}

std::vector<std::uint8_t> opusPacket()
{
    return {0xFC, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
}

std::vector<std::uint8_t> opusHead()
{
    return {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2, 0x38, 0x01, 0x80, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00};
}

void append(std::vector<std::uint8_t>& to, const gs::Expected<gs::protocol::Bytes, gs::protocol::EncodeError>& packet)
{
    REQUIRE(packet);
    to.insert(to.end(), packet->begin(), packet->end());
}

struct Crop {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

Crop busiestCrop(const gs::media::LumaSnapshot& shot, int width, int height)
{
    Crop best{.width = width, .height = height};
    double most = -1.0;
    for (int y = 8; y + height + 8 <= shot.height; y += 8) {
        for (int x = 8; x + width + 8 <= shot.width; x += 8) {
            double sum = 0.0;
            double squares = 0.0;
            for (int row = y; row < y + height; ++row) {
                for (int column = x; column < x + width; ++column) {
                    const double value = shot.luma[static_cast<std::size_t>(row) * shot.width + column];
                    sum += value;
                    squares += value * value;
                }
            }
            const double count = static_cast<double>(width) * height;
            const double spread = squares / count - (sum / count) * (sum / count);
            if (spread > most) {
                most = spread;
                best.x = x;
                best.y = y;
            }
        }
    }
    return best;
}

void writeBitmap(const fs::path& path, const std::vector<std::uint8_t>& rgb, int width, int height)
{
    const int stride = (3 * width + 3) / 4 * 4;
    const std::uint32_t pixels = static_cast<std::uint32_t>(stride * height);
    std::vector<std::uint8_t> file(54 + pixels, 0);
    const auto put16 = [&file](std::size_t at, std::uint32_t value) {
        file[at] = static_cast<std::uint8_t>(value & 0xFFU);
        file[at + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    };
    const auto put32 = [&put16](std::size_t at, std::uint32_t value) {
        put16(at, value & 0xFFFFU);
        put16(at + 2, value >> 16U);
    };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, static_cast<std::uint32_t>(file.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(height));
    put16(26, 1);
    put16(28, 24);
    put32(34, pixels);
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = file.data() + 54 + static_cast<std::size_t>(height - 1 - y) * stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = rgb.data() + 3 * (static_cast<std::size_t>(y) * width + x);
            row[3 * x] = pixel[2];
            row[3 * x + 1] = pixel[1];
            row[3 * x + 2] = pixel[0];
        }
    }
    writeBytes(path, file);
}

}

TEST_CASE("recording: the recorder appends the video packets it gets byte for byte, from the first key frame")
{
    const std::vector<std::uint8_t> original = readBytes(media("emulator_h264.media"));
    const std::vector<MediaPacket> packets = packetsOf(original);
    REQUIRE(packets.size() > 4);
    const Folder root;
    const auto clock = std::make_shared<ManualClock>(Microseconds{5'000'000});
    auto started = record::Recorder::start(record::RecorderOptions{.folder = root.path() / "whole.gsrec",
                                                                   .device = {"Emulator", 15, 288, 640, 0},
                                                                   .startedAtMs = 1'793'534'400'000,
                                                                   .frameRate = 30.0,
                                                                   .clock = clock});
    REQUIRE(started);
    const std::shared_ptr<record::Recorder> recorder = *started;
    CHECK_FALSE(record::Recorder::start(record::RecorderOptions{.folder = root.path() / "whole.gsrec"}));
    const auto firstFrame = std::ranges::find_if(packets, [](const MediaPacket& packet) {
        return packet.header.kind == MediaKind::Frame;
    });
    REQUIRE(firstFrame != packets.end());
    for (const MediaPacket& packet : packets) {
        recorder->video(packet);
    }
    clock->advance(Microseconds{1'500'000});
    const record::RecorderSummary summary = recorder->stop();
    CHECK(summary.problem.empty());
    CHECK(summary.video);
    CHECK_FALSE(summary.audio);
    CHECK(summary.durationMs == 1500);
    CHECK(summary.width == 288);
    CHECK(summary.height == 640);
    CHECK(summary.videoSessions == 2);
    CHECK(readBytes(root.path() / "whole.gsrec" / "video.media") == original);
    CHECK_FALSE(fs::exists(root.path() / "whole.gsrec" / "audio.media"));
    CHECK(summary.bytes > original.size());

    const auto opened = record::openRecording(root.path() / "whole.gsrec");
    REQUIRE(opened);
    CHECK(opened->session.video == "video.media");
    CHECK(opened->session.audio.empty());
    CHECK(opened->session.videoStartPtsUs == firstFrame->header.ptsUs);
    CHECK(opened->session.durationMs == 1500);
    CHECK(opened->session.startedAtMs == 1'793'534'400'000);
    CHECK(opened->session.device == script::DeviceInfo{"Emulator", 15, 288, 640, 0});
    CHECK(opened->video == opened->folder / "video.media");

    const auto keyAt = std::ranges::find_if(packets, [](const MediaPacket& packet) {
        return packet.header.kind == MediaKind::Session;
    });
    const auto secondSession = std::find_if(keyAt + 1, packets.end(), [](const MediaPacket& packet) {
        return packet.header.kind == MediaKind::Session;
    });
    REQUIRE(secondSession != packets.end());
    auto late = record::Recorder::start(record::RecorderOptions{.folder = root.path() / "late.gsrec", .clock = clock});
    REQUIRE(late);
    record::MediaHead head;
    std::size_t index = 0;
    for (; index < 6; ++index) {
        head.see(packets[index]);
    }
    for (const MediaPacket& earlier : head.packets(MediaChannel::Video)) {
        (*late)->video(earlier);
    }
    std::vector<std::uint8_t> expected;
    for (const MediaPacket& earlier : head.packets(MediaChannel::Video)) {
        const std::vector<std::uint8_t> bytes = record::packetBytes(earlier);
        expected.insert(expected.end(), bytes.begin(), bytes.end());
    }
    for (; index < packets.size(); ++index) {
        (*late)->video(packets[index]);
    }
    for (auto at = secondSession; at != packets.end(); ++at) {
        const std::vector<std::uint8_t> bytes = record::packetBytes(*at);
        expected.insert(expected.end(), bytes.begin(), bytes.end());
    }
    static_cast<void>((*late)->stop());
    CHECK(readBytes(root.path() / "late.gsrec" / "video.media") == expected);
}

TEST_CASE("recording: operations keep touches, keys and wheel turns, and only the length of typed text")
{
    gs::protocol::TouchFrame frame;
    frame.videoSession = 3;
    frame.points.push_back(gs::protocol::TouchPoint{.action = gs::protocol::TouchAction::Down, .pointerId = 2,
                                                    .x = 32768, .y = 65535});
    const script::SessionTouch touch = record::touchOf(frame);
    REQUIRE(touch.points.size() == 1);
    CHECK(touch.points[0] == script::SessionTouchPoint{2, script::SessionTouchAction::Down, 0.5, 1.0});
    gs::protocol::Key key;
    key.action = gs::protocol::KeyAction::Up;
    key.keycode = 66;
    key.metaState = 1;
    key.repeat = 2;
    CHECK(record::keyOf(key) == script::SessionKey{false, 66, 1, 2});
    gs::protocol::Scroll scroll;
    scroll.x = 0;
    scroll.y = 16384;
    scroll.vScroll = -1;
    CHECK(record::scrollOf(scroll) == script::SessionScroll{0.0, 0.25, 0, -1});
    CHECK(record::textOf("h\xC3\xA9llo \xE4\xB8\xAD") == script::SessionText{7});

    const Folder root;
    const auto clock = std::make_shared<ManualClock>();
    auto started = record::Recorder::start(record::RecorderOptions{.folder = root.path() / "typed.gsrec", .clock = clock});
    REQUIRE(started);
    const std::shared_ptr<record::Recorder> recorder = *started;
    clock->advance(Microseconds{100'000});
    recorder->operation(touch);
    clock->advance(Microseconds{50'000});
    recorder->operation(record::keyOf(key));
    recorder->operation(record::scrollOf(scroll));
    clock->advance(Microseconds{250'000});
    recorder->operation(record::textOf("secret words"));
    recorder->event(script::SessionEventKind::Press, "fire");
    recorder->event(script::SessionEventKind::Answer, "\"secret answer\"");
    CHECK(recorder->operationCount() == 4);
    const record::RecorderSummary summary = recorder->stop();
    CHECK(summary.operations == 4);
    CHECK(summary.events == 2);
    CHECK_FALSE(summary.video);
    recorder->operation(record::textOf("after"));
    CHECK(recorder->operationCount() == 4);

    std::ifstream file(root.path() / "typed.gsrec" / "session.json", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(text.find("secret words") == std::string::npos);
    CHECK(text.find(R"({"at":400,"text":{"length":12}})") != std::string::npos);
    const auto session = script::parseSession(text);
    REQUIRE(session);
    CHECK(session->video.empty());
    REQUIRE(session->operations.size() == 4);
    CHECK(session->operations[0] == script::SessionOperation{Microseconds{100'000}, touch});
    CHECK(session->operations[1].at == Microseconds{150'000});
    CHECK(session->operations[3] == script::SessionOperation{Microseconds{400'000}, script::SessionText{12}});
    REQUIRE(session->events.size() == 2);
    CHECK(session->events[0] == script::SessionEvent{Microseconds{400'000}, script::SessionEventKind::Press, "fire", false});
    CHECK(session->events[1].kind == script::SessionEventKind::Answer);
}

TEST_CASE("recording: a recording killed at any moment keeps a readable session and whole packets")
{
    const Folder root;
    std::mt19937 random(20261104);
    std::uniform_int_distribution<int> runFor(10, 250);
    int checked = 0;
    for (int round = 0; round < 8; ++round) {
        const fs::path folder = root.path() / std::format("killed-{}.gsrec", round);
        gs::test::ChildProcess child(GS_RECORDER_CHILD, {folder.wstring(), media("emulator_h264.media").wstring()});
        REQUIRE(child.started());
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!fs::exists(folder / "video.media") && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(runFor(random)));
        child.kill();
        CAPTURE(round);
        const auto session = script::loadSession(folder / "session.json");
        REQUIRE(session);
        record::MediaReader reader(folder / "video.media", MediaChannel::Video);
        REQUIRE(reader.opened());
        std::size_t whole = 0;
        while (const auto packet = reader.next()) {
            ++whole;
        }
        CHECK(whole > 0);
        ++checked;
    }
    CHECK(checked == 8);
}

TEST_CASE("recording: the reader stops at a packet cut short and reads whole packets up to it")
{
    const std::vector<std::uint8_t> original = readBytes(media("emulator_h265.media"));
    const std::vector<MediaPacket> packets = packetsOf(original);
    const Folder root;
    std::vector<std::uint8_t> cut(original.begin(), original.end() - 7);
    writeBytes(root.path() / "cut.media", cut);
    record::MediaReader reader(root.path() / "cut.media", MediaChannel::Video);
    REQUIRE(reader.opened());
    std::size_t whole = 0;
    while (const auto packet = reader.next()) {
        CHECK(packet->header.kind == packets[whole].header.kind);
        CHECK(packet->header.ptsUs == packets[whole].header.ptsUs);
        ++whole;
    }
    CHECK(whole == packets.size() - 1);
    CHECK(reader.truncated());
    reader.seek(0);
    CHECK_FALSE(reader.truncated());
    const auto header = reader.peek();
    REQUIRE(header);
    CHECK(header->kind == MediaKind::Session);
    reader.skip();
    CHECK(reader.position() == gs::protocol::kMediaHeaderSize + packets.front().header.size);
}

TEST_CASE("recording: MP4 export copies the stream into one video per video session, without encoding it again")
{
    const std::vector<std::uint8_t> original = readBytes(media("emulator_h264.media"));
    const std::vector<MediaPacket> packets = packetsOf(original);
    std::vector<std::size_t> frames;
    std::vector<std::pair<std::int64_t, std::int64_t>> spans;
    for (const MediaPacket& packet : packets) {
        if (packet.header.kind == MediaKind::Session) {
            frames.push_back(0);
            spans.emplace_back(-1, -1);
        } else if (packet.header.kind == MediaKind::Frame) {
            ++frames.back();
            if (spans.back().first < 0) {
                spans.back().first = packet.header.ptsUs;
            }
            spans.back().second = packet.header.ptsUs;
        }
    }
    REQUIRE(frames.size() == 2);
    const Folder root;
    const fs::path recording = root.path() / "Pixel 8 2026-11-04 12.00.00.gsrec";
    writeBytes(recording / "video.media", original);
    writeText(recording / "session.json", sessionText(spans.front().first, 6000));

    const auto exported = record::exportMp4(recording);
    REQUIRE_MESSAGE(exported, (exported ? std::string() : exported.error()));
    REQUIRE(exported->size() == 2);
    CHECK((*exported)[0].file == root.path() / "Pixel 8 2026-11-04 12.00.00.mp4");
    CHECK((*exported)[1].file == root.path() / "Pixel 8 2026-11-04 12.00.00 (2).mp4");
    CHECK_FALSE(fs::exists(root.path() / "Pixel 8 2026-11-04 12.00.00.mp4.part"));
    const std::array<std::pair<double, double>, 2> sizes{std::pair{288.0, 640.0}, std::pair{640.0, 288.0}};
    for (std::size_t index = 0; index < exported->size(); ++index) {
        CAPTURE(index);
        const record::ExportedVideo& video = (*exported)[index];
        CHECK_FALSE(video.audio);
        CHECK(video.width == static_cast<int>(sizes[index].first));
        const Movie movie = movieOf(video.file);
        REQUIRE(movie.tracks.size() == 1);
        const Track& track = movie.tracks.front();
        CHECK(track.handler == "vide");
        CHECK(track.width == sizes[index].first);
        CHECK(track.height == sizes[index].second);
        CHECK(track.samples == frames[index]);
        const double expected = static_cast<double>(video.durationUs) / 1e6;
        const double frame = static_cast<double>(spans[index].second - spans[index].first) / 1e6 / (frames[index] - 1);
        CHECK(std::abs(track.seconds - expected) <= frame);
        CHECK(std::abs(movie.seconds - expected) <= frame);
        CHECK(std::abs(expected - static_cast<double>(spans[index].second - spans[index].first) / 1e6) <= 2 * frame);
    }

    const auto again = record::exportMp4(recording);
    REQUIRE(again);
    CHECK(again->size() == 2);
    CHECK_FALSE(record::exportMp4(root.path() / "missing.gsrec"));
}

TEST_CASE("recording: MP4 export puts the recorded sound next to the picture")
{
    const std::vector<std::uint8_t> original = readBytes(media("emulator_h265.media"));
    const std::vector<MediaPacket> packets = packetsOf(original);
    std::int64_t first = -1;
    std::int64_t last = -1;
    for (const MediaPacket& packet : packets) {
        if (packet.header.kind == MediaKind::Frame) {
            first = first < 0 ? packet.header.ptsUs : first;
            last = packet.header.ptsUs;
        }
    }
    REQUIRE(first >= 0);
    std::vector<std::uint8_t> sound;
    append(sound, gs::protocol::encodeAudioSession(1, gs::protocol::AudioSessionInfo{gs::protocol::AudioCodec::Opus, 48'000, 2}));
    append(sound, gs::protocol::encodeMediaConfig(MediaChannel::Audio, 1, opusHead()));
    std::size_t clips = 0;
    for (std::int64_t at = first - 100'000; at < last; at += 20'000) {
        append(sound, gs::protocol::encodeMediaFrame(MediaChannel::Audio, 1, {}, at, 0, opusPacket()));
        clips += at >= first ? 1 : 0;
    }
    const Folder root;
    const fs::path recording = root.path() / "sound.gsrec";
    writeBytes(recording / "video.media", original);
    writeBytes(recording / "audio.media", sound);
    writeText(recording / "session.json", sessionText(first, 5000, R"(, "audio": "audio.media")"));
    const auto exported = record::exportMp4(recording);
    REQUIRE_MESSAGE(exported, (exported ? std::string() : exported.error()));
    REQUIRE(exported->size() == 1);
    CHECK(exported->front().audio);
    const Movie movie = movieOf(exported->front().file);
    REQUIRE(movie.tracks.size() == 2);
    const auto sounds = std::ranges::find(movie.tracks, std::string("soun"), &Track::handler);
    REQUIRE(sounds != movie.tracks.end());
    CHECK(sounds->samples == clips);
    CHECK(std::ranges::count(movie.tracks, std::string("vide"), &Track::handler) == 1);
}

TEST_CASE("recording: replaying a recording answers vision from the frame shown at the event's time")
{
    const std::vector<std::uint8_t> original = readBytes(media("emulator_h264.media"));
    const std::vector<MediaPacket> packets = packetsOf(original);
    std::vector<Decoded> decoded = decodeAll(packets);
    REQUIRE(decoded.size() > 12);
    const auto landscape = std::ranges::find_if(decoded, [](const Decoded& each) {
        return each.frame.width() > each.frame.height();
    });
    REQUIRE(landscape != decoded.end());
    const std::int64_t start = decoded.front().ptsUs;
    const Decoded& probe = decoded[3];
    REQUIRE(probe.frame.width() == 288);

    gs::media::FrameSnapshots snapshots;
    snapshots.offer(probe.frame);
    const auto shot = snapshots.capture(true);
    REQUIRE(shot);
    const Crop crop = busiestCrop(*shot, 96, 40);
    const std::vector<std::uint8_t> rgb = gs::media::rgbOf(*shot, crop.x, crop.y, crop.width, crop.height);
    REQUIRE(rgb.size() == static_cast<std::size_t>(3 * crop.width * crop.height));

    const Folder root;
    const fs::path logic = root.path() / "logic";
    const fs::path recording = root.path() / "probe.gsrec";
    writeBitmap(logic / "res" / "probe.bmp", rgb, crop.width, crop.height);
    writeBytes(recording / "video.media", original);
    const double regionX = (crop.x - 4.0) / shot->width;
    const double regionY = (crop.y - 4.0) / shot->height;
    const double regionWidth = (crop.width + 8.0) / shot->width;
    const double regionHeight = (crop.height + 8.0) / shot->height;
    const Microseconds seen{probe.ptsUs - start + 1'000};
    const Microseconds rotated{landscape->ptsUs - start + 1'000};

    record::RecordedVision vision(logic, recording / "video.media", start);
    REQUIRE(vision.opened());
    script::FindQuery query;
    query.image = "res/probe.bmp";
    query.file = "res/probe.bmp";
    query.threshold = 0.9;
    query.region = script::Region{regionX, regionY, regionWidth, regionHeight};
    const script::VisionAnswer early = vision.find(Microseconds{decoded[1].ptsUs - start}, query);
    CHECK(early.problem.empty());
    CHECK(vision.shownPtsUs() == decoded[1].ptsUs);
    const script::VisionAnswer found = vision.find(seen, query);
    CHECK(vision.shownPtsUs() == probe.ptsUs);
    REQUIRE(found.problem.empty());
    REQUIRE(found.json.starts_with("{"));
    const double centerX = (crop.x + crop.width / 2.0) / shot->width;
    const double centerY = (crop.y + crop.height / 2.0) / shot->height;
    const auto number = [&found](std::string_view name) {
        const std::string key = "\"" + std::string(name) + "\":";
        const std::size_t at = found.json.find(key);
        return at == std::string::npos ? -1.0 : std::stod(found.json.substr(at + key.size()));
    };
    CHECK(std::abs(number("x") - centerX) < 2.0 / shot->width);
    CHECK(std::abs(number("y") - centerY) < 2.0 / shot->height);
    CHECK(number("score") >= 0.9);
    const script::VisionAnswer stillThere = vision.find(Microseconds{decoded[4].ptsUs - start - 1}, query);
    CHECK(vision.shownPtsUs() == probe.ptsUs);
    CHECK(stillThere.json == found.json);
    const script::VisionAnswer gone = vision.find(rotated, query);
    CHECK(vision.shownPtsUs() == landscape->ptsUs);
    CHECK(gone.json == "null");
    script::PixelQuery pixel;
    pixel.at = script::Point{0.5, 0.5};
    const script::VisionAnswer color = vision.pickColor(rotated, pixel);
    CHECK(color.json.size() == 9);
    CHECK(color.json.starts_with("\"#"));
    pixel.match = true;
    pixel.rgb = script::Rgb{1, 2, 3};
    pixel.tolerance = 0.0;
    CHECK(vision.pickColor(rotated, pixel).json == "false");
    script::FindQuery missing = query;
    missing.image = "res/missing.png";
    missing.file = "res/missing.png";
    CHECK(vision.find(rotated, missing).problem.starts_with("GS-API-016 res/missing.png cannot be read"));

    writeText(logic / "main.js", std::format(R"js(
controls.probe.onPress(async () => {{
    const seen = await vision.find("res/probe.bmp", {{ threshold: 0.9, region: {{ x: {}, y: {}, width: {}, height: {} }} }});
    log.info(seen === null ? "missing" : "seen");
}});
controls.other.onPress(async () => {{
    try {{
        await vision.find("res/missing.png");
    }} catch (error) {{
        log.info("refused");
    }}
}});
)js",
                                             regionX, regionY, regionWidth, regionHeight));
    ManualClock clock;
    script::RecordingServices services(clock, script::DeviceInfo{"Emulator", 15, 288, 640, 0},
                                       {{"probe", "probe", "tap", true}, {"other", "other", "tap", true}}, {});
    record::RecordedVision replayed(logic, recording / "video.media", start);
    services.setVision(&replayed);
    std::vector<std::string> logged;
    const script::ScriptHost::Sink sink = [&logged](const script::Report& report) {
        if (report.kind == script::ReportKind::Log) {
            logged.push_back(report.message);
        }
    };
    script::ReplayOptions options;
    options.source = script::ScriptSource{.folder = logic, .entry = "main.js"};
    options.events = {
        script::SessionEvent{seen, script::SessionEventKind::Press, "probe", true},
        script::SessionEvent{seen + Microseconds{1'000}, script::SessionEventKind::Release, "probe", false},
        script::SessionEvent{rotated, script::SessionEventKind::Press, "probe", true},
        script::SessionEvent{rotated + Microseconds{1'000}, script::SessionEventKind::Release, "probe", false},
        script::SessionEvent{rotated + Microseconds{2'000}, script::SessionEventKind::Press, "other", true},
    };
    options.until = rotated + Microseconds{500'000};
    const script::ReplayResult result = script::replay(options, services, clock, sink);
    CHECK(result.loaded);
    CHECK(logged == std::vector<std::string>{"seen", "missing", "refused"});
}

TEST_CASE("recording: recording folders and the videos exported from them are named after the recording")
{
    CHECK(record::safeFileName("Pixel 8 Pro") == "Pixel 8 Pro");
    CHECK(record::safeFileName("a<b>c:d\"e/f\\g|h?i*j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK(record::safeFileName("  phone. ") == "phone");
    CHECK(record::safeFileName("") == "Phone");
    CHECK(record::exportPath("D:/Videos/GameScrcpy/Pixel 2026-11-04 10.30.00.gsrec", 0)
          == fs::path("D:/Videos/GameScrcpy/Pixel 2026-11-04 10.30.00.mp4"));
    CHECK(record::exportPath("D:/Videos/GameScrcpy/Pixel 2026-11-04 10.30.00.gsrec/", 2)
          == fs::path("D:/Videos/GameScrcpy/Pixel 2026-11-04 10.30.00 (3).mp4"));
    CHECK(record::exportPath("D:/Videos/demo", 1) == fs::path("D:/Videos/demo (2).mp4"));
    const Folder root;
    CHECK_FALSE(record::isRecordingFolder(root.path()));
    writeText(root.path() / "session.json", sessionText(0, 0));
    CHECK(record::isRecordingFolder(root.path()));
    CHECK_FALSE(record::openRecording(root.path() / "missing"));
}
