#include "gs/protocol/ReedSolomon.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace gs::protocol {

namespace {

struct Field {
    std::array<std::uint8_t, 512> exp{};
    std::array<std::uint8_t, 256> log{};
};

constexpr Field makeField()
{
    Field field;
    unsigned value = 1;
    for (unsigned power = 0; power < 255; ++power) {
        field.exp[power] = static_cast<std::uint8_t>(value);
        field.log[value] = static_cast<std::uint8_t>(power);
        value <<= 1;
        if ((value & 0x100U) != 0) {
            value ^= 0x11DU;
        }
    }
    for (unsigned power = 255; power < 512; ++power) {
        field.exp[power] = field.exp[power - 255];
    }
    return field;
}

constexpr Field kField = makeField();

constexpr std::uint8_t multiply(std::uint8_t a, std::uint8_t b) noexcept
{
    return a == 0 || b == 0 ? 0 : kField.exp[kField.log[a] + kField.log[b]];
}

constexpr std::uint8_t invert(std::uint8_t a) noexcept
{
    return kField.exp[255 - kField.log[a]];
}

void addScaled(std::uint8_t* out, const std::uint8_t* in, std::uint8_t coefficient, std::size_t length) noexcept
{
    if (coefficient == 0) {
        return;
    }
    const unsigned scale = kField.log[coefficient];
    for (std::size_t b = 0; b < length; ++b) {
        if (in[b] != 0) {
            out[b] ^= kField.exp[scale + kField.log[in[b]]];
        }
    }
}

}

ReedSolomon::ReedSolomon(int dataShards, int parityShards)
    : m_data(dataShards),
      m_parity(parityShards),
      m_cauchy(static_cast<std::size_t>(std::max(0, dataShards * parityShards)))
{
    if (dataShards < 1 || parityShards < 0 || dataShards + parityShards > kMaxShards) {
        std::abort();
    }
    for (int i = 0; i < m_parity; ++i) {
        for (int j = 0; j < m_data; ++j) {
            m_cauchy[static_cast<std::size_t>(i * m_data + j)] = invert(static_cast<std::uint8_t>((m_data + i) ^ j));
        }
    }
}

void ReedSolomon::encode(std::span<std::uint8_t* const> shards, std::size_t length) const
{
    for (int i = 0; i < m_parity; ++i) {
        std::uint8_t* const out = shards[static_cast<std::size_t>(m_data + i)];
        std::fill_n(out, length, std::uint8_t{0});
        for (int j = 0; j < m_data; ++j) {
            addScaled(out, shards[static_cast<std::size_t>(j)], m_cauchy[static_cast<std::size_t>(i * m_data + j)],
                      length);
        }
    }
}

bool ReedSolomon::reconstruct(std::span<std::uint8_t* const> shards, const std::bitset<kMaxShards>& present,
                              std::size_t length) const
{
    const auto k = static_cast<std::size_t>(m_data);
    bool dataMissing = false;
    for (std::size_t d = 0; d < k; ++d) {
        dataMissing = dataMissing || !present[d];
    }
    if (!dataMissing) {
        return true;
    }
    std::vector<std::size_t> used;
    used.reserve(k);
    for (std::size_t i = 0; i < shards.size() && used.size() < k; ++i) {
        if (present[i]) {
            used.push_back(i);
        }
    }
    if (used.size() < k) {
        return false;
    }
    std::vector<std::uint8_t> matrix(k * k);
    std::vector<std::uint8_t> inverse(k * k);
    for (std::size_t t = 0; t < k; ++t) {
        if (used[t] < k) {
            matrix[t * k + used[t]] = 1;
        } else {
            std::copy_n(m_cauchy.begin() + static_cast<std::ptrdiff_t>((used[t] - k) * k), k,
                        matrix.begin() + static_cast<std::ptrdiff_t>(t * k));
        }
        inverse[t * k + t] = 1;
    }
    for (std::size_t column = 0; column < k; ++column) {
        std::size_t pivot = column;
        while (matrix[pivot * k + column] == 0) {
            ++pivot;
        }
        if (pivot != column) {
            std::swap_ranges(matrix.begin() + static_cast<std::ptrdiff_t>(pivot * k),
                             matrix.begin() + static_cast<std::ptrdiff_t>((pivot + 1) * k),
                             matrix.begin() + static_cast<std::ptrdiff_t>(column * k));
            std::swap_ranges(inverse.begin() + static_cast<std::ptrdiff_t>(pivot * k),
                             inverse.begin() + static_cast<std::ptrdiff_t>((pivot + 1) * k),
                             inverse.begin() + static_cast<std::ptrdiff_t>(column * k));
        }
        const std::uint8_t scale = invert(matrix[column * k + column]);
        for (std::size_t c = 0; c < k; ++c) {
            matrix[column * k + c] = multiply(scale, matrix[column * k + c]);
            inverse[column * k + c] = multiply(scale, inverse[column * k + c]);
        }
        for (std::size_t r = 0; r < k; ++r) {
            const std::uint8_t factor = matrix[r * k + column];
            if (r == column || factor == 0) {
                continue;
            }
            for (std::size_t c = 0; c < k; ++c) {
                matrix[r * k + c] ^= multiply(factor, matrix[column * k + c]);
                inverse[r * k + c] ^= multiply(factor, inverse[column * k + c]);
            }
        }
    }
    for (std::size_t d = 0; d < k; ++d) {
        if (present[d]) {
            continue;
        }
        std::uint8_t* const out = shards[d];
        std::fill_n(out, length, std::uint8_t{0});
        for (std::size_t t = 0; t < k; ++t) {
            addScaled(out, shards[used[t]], inverse[d * k + t], length);
        }
    }
    return true;
}

}
