#pragma once

#include <gs/protocol/Datagram.h>
#include <gs/protocol/DirectMedia.h>
#include <gs/protocol/ReedSolomon.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gs::protocol::testing {

inline std::vector<std::vector<std::uint8_t>> videoShards(std::uint32_t number, const std::vector<std::uint8_t>& packet,
                                                          std::uint16_t shardSize, std::uint8_t parity,
                                                          std::uint32_t connection = 7)
{
    const std::size_t count = (packet.size() + shardSize - 1) / shardSize;
    std::vector<std::vector<std::uint8_t>> out;
    for (std::size_t group = 0; group * kShardsPerGroup < count; ++group) {
        const auto k = static_cast<std::uint8_t>(std::min(kShardsPerGroup, count - group * kShardsPerGroup));
        std::vector<std::vector<std::uint8_t>> shards(k + parity, std::vector<std::uint8_t>(shardSize, 0));
        std::vector<std::size_t> sizes(k + parity, shardSize);
        for (std::size_t i = 0; i < k; ++i) {
            const std::size_t at = (group * kShardsPerGroup + i) * shardSize;
            sizes[i] = std::min<std::size_t>(shardSize, packet.size() - at);
            std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(at), sizes[i], shards[i].begin());
        }
        if (parity != 0) {
            std::vector<std::uint8_t*> pointers;
            for (auto& shard : shards) {
                pointers.push_back(shard.data());
            }
            ReedSolomon(k, parity).encode(pointers, shardSize);
        }
        for (std::size_t index = 0; index < shards.size(); ++index) {
            const auto header = encodeDatagramHeader({.channel = kVideoChannel, .connection = connection});
            std::vector<std::uint8_t> datagram(header.begin(), header.end());
            const auto put = [&datagram](std::uint64_t value, int bytes) {
                for (int shift = 8 * (bytes - 1); shift >= 0; shift -= 8) {
                    datagram.push_back(static_cast<std::uint8_t>(value >> shift));
                }
            };
            put(number, 4);
            put(packet.size(), 4);
            put(shardSize, 2);
            put(group, 2);
            put(index, 1);
            put(k, 1);
            put(parity, 1);
            put(0, 1);
            datagram.insert(datagram.end(), shards[index].begin(),
                            shards[index].begin() + static_cast<std::ptrdiff_t>(sizes[index]));
            out.push_back(std::move(datagram));
        }
    }
    return out;
}

}
