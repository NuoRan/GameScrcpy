#include <gs/protocol/DirectMedia.h>

#include <gs/base/ByteOrder.h>
#include <gs/protocol/ReedSolomon.h>

#include <algorithm>
#include <bitset>
#include <utility>

namespace gs::protocol {

Expected<VideoShard, DecodeError> decodeVideoShard(std::span<const std::uint8_t> datagram) noexcept
{
    if (datagram.size() < kDatagramHeaderSize + kShardHeaderSize) {
        return makeUnexpected(DecodeError::Truncated);
    }
    const auto header = datagram.subspan(kDatagramHeaderSize, kShardHeaderSize);
    VideoShard shard;
    shard.packet = loadBigEndian<std::uint32_t>(header.first<4>());
    shard.packetSize = loadBigEndian<std::uint32_t>(header.subspan<4, 4>());
    shard.shardSize = loadBigEndian<std::uint16_t>(header.subspan<8, 2>());
    shard.group = loadBigEndian<std::uint16_t>(header.subspan<10, 2>());
    shard.index = header[12];
    shard.k = header[13];
    shard.m = header[14];
    if (header[15] != 0) {
        return makeUnexpected(DecodeError::ReservedBitsSet);
    }
    if (shard.packetSize == 0 || shard.packetSize > kMaxMediaPacketSize || shard.shardSize == 0) {
        return makeUnexpected(DecodeError::OutOfRange);
    }
    const std::uint32_t count = (shard.packetSize + shard.shardSize - 1) / shard.shardSize;
    const std::uint32_t groups = (count + kShardsPerGroup - 1) / kShardsPerGroup;
    const std::uint32_t first = static_cast<std::uint32_t>(shard.group) * kShardsPerGroup;
    if (shard.group >= groups || shard.k != std::min<std::uint32_t>(kShardsPerGroup, count - first) ||
        shard.k + shard.m > ReedSolomon::kMaxShards || shard.index >= shard.k + shard.m) {
        return makeUnexpected(DecodeError::OutOfRange);
    }
    const std::size_t expected =
        shard.index < shard.k
            ? std::min<std::size_t>(shard.shardSize,
                                    shard.packetSize - static_cast<std::size_t>(first + shard.index) * shard.shardSize)
            : shard.shardSize;
    const auto bytes = datagram.subspan(kDatagramHeaderSize + kShardHeaderSize);
    if (bytes.size() < expected) {
        return makeUnexpected(DecodeError::Truncated);
    }
    if (bytes.size() > expected) {
        return makeUnexpected(DecodeError::TrailingBytes);
    }
    shard.bytes = bytes;
    return shard;
}

std::array<std::uint8_t, kMediaClosedSize> encodeMediaClosed(std::uint8_t channel, std::uint32_t connection) noexcept
{
    std::array<std::uint8_t, kMediaClosedSize> out{};
    encodeDatagramHeader({.channel = channel, .connection = connection},
                         std::span<std::uint8_t, kDatagramHeaderSize>(out.data(), kDatagramHeaderSize));
    return out;
}

bool isMediaClosed(std::span<const std::uint8_t> datagram) noexcept
{
    return datagram.size() == kMediaClosedSize && datagram.back() == 0;
}

Expected<AudioDatagram, DecodeError> decodeAudioDatagram(std::span<const std::uint8_t> datagram) noexcept
{
    if (datagram.size() < kDatagramHeaderSize + kAudioHeaderSize) {
        return makeUnexpected(DecodeError::Truncated);
    }
    AudioDatagram audio;
    audio.packet = loadBigEndian<std::uint32_t>(datagram.subspan<kDatagramHeaderSize, 4>());
    audio.count = datagram[kDatagramHeaderSize + 4];
    if ((audio.count != 1 && audio.count != 2) || (audio.count == 2 && audio.packet == 0)) {
        return makeUnexpected(DecodeError::OutOfRange);
    }
    auto rest = datagram.subspan(kDatagramHeaderSize + kAudioHeaderSize);
    for (std::uint8_t i = 0; i < audio.count; ++i) {
        if (rest.size() < 2) {
            return makeUnexpected(DecodeError::Truncated);
        }
        const auto size = loadBigEndian<std::uint16_t>(rest.first<2>());
        if (size == 0) {
            return makeUnexpected(DecodeError::OutOfRange);
        }
        if (rest.size() - 2 < size) {
            return makeUnexpected(DecodeError::Truncated);
        }
        audio.packets[i] = rest.subspan(2, size);
        rest = rest.subspan(2 + static_cast<std::size_t>(size));
    }
    if (!rest.empty()) {
        return makeUnexpected(DecodeError::TrailingBytes);
    }
    return audio;
}

void VideoAssembler::add(const VideoShard& shard)
{
    if (shard.packet < m_next) {
        if (const auto tally = m_tallies.find(shard.packet);
            tally != m_tallies.end() && tally->second.received < tally->second.expected) {
            ++tally->second.received;
        }
        return;
    }
    auto [entry, created] = m_pending.try_emplace(shard.packet);
    Pending& pending = entry->second;
    if (created) {
        pending.packetSize = shard.packetSize;
        pending.shardSize = shard.shardSize;
        const std::uint32_t count = (shard.packetSize + shard.shardSize - 1) / shard.shardSize;
        pending.groups.resize((count + kShardsPerGroup - 1) / kShardsPerGroup);
        while (m_pending.size() > kMaxPending) {
            giveUpBefore(m_pending.begin()->first + 1);
        }
        if (!m_pending.contains(shard.packet)) {
            return;
        }
    } else if (pending.packetSize != shard.packetSize || pending.shardSize != shard.shardSize) {
        return;
    }
    if (pending.complete) {
        return;
    }
    auto& slot = pending.groups[shard.group];
    if (!slot) {
        slot.emplace();
        slot->k = shard.k;
        slot->m = shard.m;
        slot->shards.assign(static_cast<std::size_t>(shard.k + shard.m) * shard.shardSize, 0);
        slot->present.assign(static_cast<std::size_t>(shard.k + shard.m), false);
    } else if (slot->k != shard.k || slot->m != shard.m) {
        return;
    }
    Group& group = *slot;
    if (group.present[shard.index]) {
        return;
    }
    group.present[shard.index] = true;
    ++group.received;
    if (group.done) {
        return;
    }
    std::ranges::copy(shard.bytes, group.shards.begin() + static_cast<std::ptrdiff_t>(shard.index) * shard.shardSize);
    if (group.received < group.k) {
        return;
    }
    settle(group, shard.shardSize);
    if (++pending.groupsDone < pending.groups.size()) {
        return;
    }
    pending.complete = true;
    giveUpBefore(shard.packet);
}

void VideoAssembler::settle(Group& group, std::uint16_t shardSize)
{
    const std::size_t total = static_cast<std::size_t>(group.k) + group.m;
    std::size_t missingData = 0;
    for (std::size_t i = 0; i < group.k; ++i) {
        missingData += group.present[i] ? 0 : 1;
    }
    if (missingData != 0) {
        std::vector<std::uint8_t*> shards(total);
        std::bitset<ReedSolomon::kMaxShards> present;
        for (std::size_t i = 0; i < total; ++i) {
            shards[i] = group.shards.data() + i * shardSize;
            present[i] = group.present[i];
        }
        [[maybe_unused]] const bool rebuilt =
            ReedSolomon(group.k, group.m).reconstruct(shards, present, shardSize);
        m_stats.shardsRecovered += missingData;
    }
    group.done = true;
}

void VideoAssembler::giveUpBefore(std::uint32_t packet)
{
    while (!m_tallies.empty() && m_tallies.begin()->first < packet) {
        m_stats.shardsLost += m_tallies.begin()->second.expected - m_tallies.begin()->second.received;
        m_tallies.erase(m_tallies.begin());
    }
    while (!m_pending.empty() && m_pending.begin()->first < packet && !m_pending.begin()->second.complete) {
        const Pending& lost = m_pending.begin()->second;
        const std::uint32_t count = (lost.packetSize + lost.shardSize - 1) / lost.shardSize;
        for (std::size_t g = 0; g < lost.groups.size(); ++g) {
            const auto& group = lost.groups[g];
            if (!group) {
                m_stats.shardsLost += std::min<std::size_t>(kShardsPerGroup, count - g * kShardsPerGroup);
            } else if (!group->done) {
                m_stats.shardsLost += static_cast<std::size_t>(group->k) + group->m - group->received;
            }
        }
        m_pending.erase(m_pending.begin());
    }
    deliverInOrder();
}

void VideoAssembler::deliverInOrder()
{
    while (!m_pending.empty()) {
        auto first = m_pending.begin();
        if (!first->second.complete) {
            return;
        }
        const std::uint32_t skipped = first->first - m_next;
        m_lost += skipped;
        m_stats.packetsLost += skipped;
        Tally tally;
        for (const auto& group : first->second.groups) {
            tally.expected += static_cast<std::size_t>(group->k) + group->m;
            tally.received += group->received;
        }
        m_tallies.emplace(first->first, tally);
        m_ready.push_back(MediaDelivery{assemble(first->second), m_lost});
        m_lost = 0;
        m_next = first->first + 1;
        m_pending.erase(first);
    }
}

std::vector<std::uint8_t> VideoAssembler::assemble(Pending& pending)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(pending.packetSize);
    for (const auto& group : pending.groups) {
        bytes.insert(bytes.end(), group->shards.begin(),
                     group->shards.begin() + static_cast<std::ptrdiff_t>(group->k) * pending.shardSize);
    }
    bytes.resize(pending.packetSize);
    return bytes;
}

std::optional<MediaDelivery> VideoAssembler::next()
{
    if (m_ready.empty()) {
        return std::nullopt;
    }
    MediaDelivery delivery = std::move(m_ready.front());
    m_ready.pop_front();
    return delivery;
}

void AudioAssembler::add(const AudioDatagram& datagram)
{
    for (std::uint8_t i = 0; i < datagram.count; ++i) {
        const std::uint32_t number = datagram.packet - i;
        if (number >= m_next && !m_waiting.contains(number)) {
            m_waiting.emplace(number, std::vector<std::uint8_t>(datagram.packets[i].begin(), datagram.packets[i].end()));
        }
    }
    const std::uint32_t oldestPossible = datagram.count == 2 ? datagram.packet - 1 : datagram.packet;
    while (m_next <= datagram.packet) {
        const auto found = m_waiting.find(m_next);
        if (found != m_waiting.end()) {
            m_ready.push_back(MediaDelivery{std::move(found->second), m_lost});
            m_lost = 0;
            m_waiting.erase(found);
        } else if (m_next < oldestPossible) {
            ++m_lost;
            ++m_packetsLost;
        } else {
            break;
        }
        ++m_next;
    }
}

std::optional<MediaDelivery> AudioAssembler::next()
{
    if (m_ready.empty()) {
        return std::nullopt;
    }
    MediaDelivery delivery = std::move(m_ready.front());
    m_ready.pop_front();
    return delivery;
}

}
