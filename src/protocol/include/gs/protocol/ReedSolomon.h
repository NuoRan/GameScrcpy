#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gs::protocol {

class ReedSolomon
{
public:
    static constexpr int kMaxShards = 256;

    ReedSolomon(int dataShards, int parityShards);

    [[nodiscard]] int dataShards() const noexcept { return m_data; }
    [[nodiscard]] int parityShards() const noexcept { return m_parity; }

    void encode(std::span<std::uint8_t* const> shards, std::size_t length) const;

    [[nodiscard]] bool reconstruct(std::span<std::uint8_t* const> shards, const std::bitset<kMaxShards>& present,
                                   std::size_t length) const;

private:
    int m_data;
    int m_parity;
    std::vector<std::uint8_t> m_cauchy;
};

}
