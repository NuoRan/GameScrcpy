#include "gs/media/AudioPlayer.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <span>

using namespace gs;

namespace {

constexpr std::array<std::uint8_t, 19> kOpusHead{'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2, 0x38, 0x01,
                                                  0x80, 0xBB, 0x00, 0x00, 0x00, 0x00, 0};
constexpr std::array<std::uint8_t, 3> kSilence{0xFC, 0xFF, 0xFE};

protocol::MediaPacket packet(protocol::MediaKind kind, std::span<const std::uint8_t> payload = {},
                             std::int64_t ptsUs = 0)
{
    protocol::MediaPacket result;
    result.header.kind = kind;
    result.header.session = 1;
    result.header.ptsUs = ptsUs;
    result.header.size = static_cast<std::uint32_t>(payload.size());
    result.payload = payload;
    if (kind == protocol::MediaKind::Session) {
        protocol::AudioSessionInfo info;
        info.sampleRate = 48'000;
        info.channels = 2;
        result.session = info;
    }
    return result;
}

}

TEST_CASE("audio packets out of order are refused")
{
    media::AudioPlayer player;
    CHECK_FALSE(player.push(packet(protocol::MediaKind::Frame, kSilence)));
    CHECK_FALSE(player.push(packet(protocol::MediaKind::Config, kOpusHead)));
    CHECK(player.stats().targetFrames == 0);
}

TEST_CASE("Opus packets are decoded into the playback buffer, which waits for the target")
{
    media::AudioPlayer player;
    REQUIRE(player.push(packet(protocol::MediaKind::Session)));
    REQUIRE(player.push(packet(protocol::MediaKind::Config, kOpusHead)));
    REQUIRE(player.push(packet(protocol::MediaKind::Frame, kSilence, 0)));
    REQUIRE(player.push(packet(protocol::MediaKind::Frame, kSilence, 20'000)));

    const media::AudioStats stats = player.stats();
    CHECK(stats.sampleRate == 48'000);
    CHECK(stats.targetFrames == 2'400);
    CHECK(stats.bufferedFrames > 960);
    CHECK(stats.bufferedFrames <= 2 * 960);
    CHECK(stats.underflowFrames == 0);

    player.close();
    CHECK(player.stats().bufferedFrames == 0);
    CHECK_FALSE(player.push(packet(protocol::MediaKind::Frame, kSilence, 40'000)));
}
