#include <gs/protocol/Codec.h>
#include <gs/protocol/Constants.h>
#include <gs/record/MediaFile.h>
#include <gs/record/Mp4Export.h>
#include <gs/record/Recorder.h>
#include <gs/record/Recording.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using gs::protocol::AudioSessionInfo;
using gs::protocol::MediaChannel;
using gs::protocol::MediaKind;
using gs::protocol::MediaPacket;
namespace fs = std::filesystem;
namespace record = gs::record;

namespace {

class Folder
{
public:
    Folder()
        : m_root(fs::temp_directory_path() /
                 ("gs_aac_" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}())))
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

std::vector<std::uint8_t> readBytes(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

constexpr std::array<std::uint8_t, 2> kConfig{0x11, 0x90};
constexpr std::array<std::uint8_t, 6> kClip{0x21, 0x10, 0x04, 0x60, 0x8C, 0x1C};

MediaPacket aacSession(std::uint16_t session, const std::vector<std::uint8_t>& payload)
{
    MediaPacket packet;
    packet.header.kind = MediaKind::Session;
    packet.header.session = session;
    packet.header.size = static_cast<std::uint32_t>(payload.size());
    packet.session = record::aacSessionInfo(48'000, 2);
    packet.payload = payload;
    return packet;
}

MediaPacket aacPacket(MediaKind kind, std::uint16_t session, std::int64_t ptsUs, std::span<const std::uint8_t> bytes)
{
    MediaPacket packet;
    packet.header.kind = kind;
    packet.header.keyFrame = kind == MediaKind::Frame;
    packet.header.session = session;
    packet.header.ptsUs = ptsUs;
    packet.header.size = static_cast<std::uint32_t>(bytes.size());
    packet.payload = bytes;
    return packet;
}

}

TEST_CASE("recording: AAC sound keeps its own session marker on disk, which the protocol does not know, and reads back")
{
    const std::vector<std::uint8_t> payload = record::aacSessionPayload(48'000, 2);
    REQUIRE(payload.size() == record::kAudioSessionSize);
    CHECK(payload[0] == record::kRecordedAac);
    const std::vector<std::uint8_t> bytes = record::packetBytes(aacSession(4, payload));
    CHECK_FALSE(gs::protocol::decodeMediaPacket(MediaChannel::Audio, bytes));
    const std::optional<MediaPacket> read = record::decodeRecorded(MediaChannel::Audio, bytes);
    REQUIRE(read);
    CHECK(read->header.kind == MediaKind::Session);
    CHECK(read->header.session == 4);
    const auto* info = std::get_if<AudioSessionInfo>(&read->session);
    REQUIRE(info != nullptr);
    CHECK(record::isRecordedAac(*info));
    CHECK(info->sampleRate == 48'000);
    CHECK(info->channels == 2);
    CHECK_FALSE(record::decodeRecorded(MediaChannel::Video, bytes));

    std::vector<std::uint8_t> silent = record::aacSessionPayload(0, 2);
    CHECK_FALSE(record::decodeRecorded(MediaChannel::Audio, record::packetBytes(aacSession(4, silent))));
    std::vector<std::uint8_t> other = payload;
    other[0] = 0x42;
    CHECK_FALSE(record::decodeRecorded(MediaChannel::Audio, record::packetBytes(aacSession(4, other))));
}

TEST_CASE("recording: AAC sound from a capture card is recorded next to the picture and exported into the MP4 (MED-014)")
{
    const std::vector<std::uint8_t> original = readBytes(fs::path(GS_VIDEO_TEST_DATA_DIR) / "emulator_h265.media");
    REQUIRE_FALSE(original.empty());
    const Folder root;
    auto started = record::Recorder::start(record::RecorderOptions{.folder = root.path() / "card.gsrec"});
    REQUIRE(started);
    const std::shared_ptr<record::Recorder> recorder = *started;

    std::int64_t first = -1;
    std::int64_t last = -1;
    for (std::size_t offset = 0; offset < original.size();) {
        const auto packet = gs::protocol::decodeMediaPacket(MediaChannel::Video, std::span(original).subspan(offset));
        REQUIRE(packet);
        offset += gs::protocol::kMediaHeaderSize + packet->header.size;
        if (packet->header.kind == MediaKind::Frame) {
            first = first < 0 ? packet->header.ptsUs : first;
            last = packet->header.ptsUs;
        }
        recorder->video(*packet);
    }
    REQUIRE(first >= 0);

    const std::vector<std::uint8_t> payload = record::aacSessionPayload(48'000, 2);
    recorder->audio(aacSession(1, payload));
    recorder->audio(aacPacket(MediaKind::Config, 1, 0, kConfig));
    std::size_t clips = 0;
    for (std::int64_t at = first; at < last; at += 21'333) {
        recorder->audio(aacPacket(MediaKind::Frame, 1, at, kClip));
        ++clips;
    }
    REQUIRE(clips > 10);
    const record::RecorderSummary summary = recorder->stop();
    CHECK(summary.problem.empty());
    CHECK(summary.audio);
    CHECK(summary.video);

    record::MediaReader reader(root.path() / "card.gsrec" / record::kAudioFile, MediaChannel::Audio);
    REQUIRE(reader.opened());
    const std::optional<MediaPacket> described = reader.next();
    REQUIRE(described);
    REQUIRE(described->header.kind == MediaKind::Session);
    const auto* info = std::get_if<AudioSessionInfo>(&described->session);
    REQUIRE(info != nullptr);
    CHECK(record::isRecordedAac(*info));
    const std::optional<MediaPacket> configured = reader.next();
    REQUIRE(configured);
    CHECK(configured->header.kind == MediaKind::Config);
    CHECK(std::ranges::equal(configured->payload, kConfig));
    std::size_t frames = 0;
    while (const std::optional<MediaPacket> clip = reader.next()) {
        CHECK(clip->header.kind == MediaKind::Frame);
        CHECK(std::ranges::equal(clip->payload, kClip));
        ++frames;
    }
    CHECK(frames == clips);
    CHECK_FALSE(reader.truncated());

    const auto exported = record::exportMp4(root.path() / "card.gsrec");
    REQUIRE_MESSAGE(exported, (exported ? std::string() : exported.error()));
    REQUIRE(exported->size() == 1);
    CHECK(exported->front().audio);
    const std::vector<std::uint8_t> movie = readBytes(exported->front().file);
    const std::string_view text(reinterpret_cast<const char*>(movie.data()), movie.size());
    CHECK(text.find("mp4a") != std::string_view::npos);
    CHECK(text.find("Opus") == std::string_view::npos);
}
