#pragma once

#include <gs/base/Expected.h>
#include <gs/protocol/Datagram.h>
#include <gs/protocol/Errors.h>
#include <gs/protocol/Reliable.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace gs::protocol {

inline constexpr std::uint8_t kVideoChannel = 1;
inline constexpr std::uint8_t kAudioChannel = 2;
inline constexpr std::size_t kShardHeaderSize = 16;
inline constexpr std::size_t kMaxShardSize = kReliableDatagramLimit - kDatagramHeaderSize - kShardHeaderSize;
inline constexpr std::size_t kShardsPerGroup = 64;
inline constexpr std::uint32_t kMaxMediaPacketSize = (16u << 20) + 20;
inline constexpr std::size_t kAudioHeaderSize = 5;
inline constexpr std::size_t kMediaClosedSize = kDatagramHeaderSize + 1;

[[nodiscard]] std::array<std::uint8_t, kMediaClosedSize> encodeMediaClosed(std::uint8_t channel,
                                                                           std::uint32_t connection) noexcept;
[[nodiscard]] bool isMediaClosed(std::span<const std::uint8_t> datagram) noexcept;

struct VideoShard {
    std::uint32_t packet = 0;
    std::uint32_t packetSize = 0;
    std::uint16_t shardSize = 0;
    std::uint16_t group = 0;
    std::uint8_t index = 0;
    std::uint8_t k = 0;
    std::uint8_t m = 0;
    std::span<const std::uint8_t> bytes;
};

[[nodiscard]] Expected<VideoShard, DecodeError> decodeVideoShard(std::span<const std::uint8_t> datagram) noexcept;

struct AudioDatagram {
    std::uint32_t packet = 0;
    std::uint8_t count = 0;
    std::array<std::span<const std::uint8_t>, 2> packets;
};

[[nodiscard]] Expected<AudioDatagram, DecodeError> decodeAudioDatagram(std::span<const std::uint8_t> datagram) noexcept;

struct MediaDelivery {
    std::vector<std::uint8_t> bytes;
    std::uint32_t lostBefore = 0;
};

class VideoAssembler
{
public:
    static constexpr std::size_t kMaxPending = 64;

    struct Stats {
        std::uint64_t shardsLost = 0;
        std::uint64_t shardsRecovered = 0;
        std::uint64_t packetsLost = 0;
    };

    void add(const VideoShard& shard);
    [[nodiscard]] std::optional<MediaDelivery> next();
    [[nodiscard]] const Stats& stats() const noexcept { return m_stats; }

private:
    struct Group {
        std::uint8_t k = 0;
        std::uint8_t m = 0;
        std::size_t received = 0;
        bool done = false;
        std::vector<std::uint8_t> shards;
        std::vector<bool> present;
    };
    struct Pending {
        std::uint32_t packetSize = 0;
        std::uint16_t shardSize = 0;
        std::size_t groupsDone = 0;
        std::vector<std::optional<Group>> groups;
        bool complete = false;
    };
    struct Tally {
        std::size_t expected = 0;
        std::size_t received = 0;
    };

    void settle(Group& group, std::uint16_t shardSize);
    void giveUpBefore(std::uint32_t packet);
    void deliverInOrder();
    [[nodiscard]] static std::vector<std::uint8_t> assemble(Pending& pending);

    std::map<std::uint32_t, Pending> m_pending;
    std::map<std::uint32_t, Tally> m_tallies;
    std::uint32_t m_next = 0;
    std::uint32_t m_lost = 0;
    std::deque<MediaDelivery> m_ready;
    Stats m_stats;
};

class AudioAssembler
{
public:
    void add(const AudioDatagram& datagram);
    [[nodiscard]] std::optional<MediaDelivery> next();
    [[nodiscard]] std::uint64_t packetsLost() const noexcept { return m_packetsLost; }

private:
    std::map<std::uint32_t, std::vector<std::uint8_t>> m_waiting;
    std::uint32_t m_next = 0;
    std::uint32_t m_lost = 0;
    std::deque<MediaDelivery> m_ready;
    std::uint64_t m_packetsLost = 0;
};

}
