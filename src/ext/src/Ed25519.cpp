#include "gs/ext/Minisign.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::ext {

namespace {

constexpr std::array<std::uint64_t, 80> kRounds{
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
    0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
    0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
    0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
    0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
    0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817};

constexpr std::array<std::uint64_t, 8> kStart{0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b,
                                              0xa54ff53a5f1d36f1, 0x510e527fade682d1, 0x9b05688c2b3e6c1f,
                                              0x1f83d9abfb41bd6b, 0x5be0cd19137e2179};

constexpr std::array<std::array<std::uint8_t, 16>, 10> kSigma{{
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
}};

using Field = std::array<std::int64_t, 16>;
using Point = std::array<Field, 4>;
using Encoded = std::array<std::uint8_t, 32>;

constexpr Field kZero{};
constexpr Field kOne{1};
constexpr Field kD{0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                   0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
constexpr Field kD2{0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                    0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
constexpr Field kBaseX{0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                       0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
constexpr Field kBaseY{0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                       0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
constexpr Field kRootOfMinusOne{0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                                0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
constexpr std::array<std::int64_t, 32> kOrder{0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                              0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                                              0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

const std::uint8_t* bytesOf(std::string_view data) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(data.data());
}

void sha512Block(std::array<std::uint64_t, 8>& state, const std::uint8_t* block) noexcept
{
    std::array<std::uint64_t, 80> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        std::uint64_t word = 0;
        for (std::size_t k = 0; k < 8; ++k) {
            word = (word << 8) | block[8 * i + k];
        }
        w[i] = word;
    }
    for (std::size_t i = 16; i < 80; ++i) {
        const std::uint64_t low = std::rotr(w[i - 15], 1) ^ std::rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const std::uint64_t high = std::rotr(w[i - 2], 19) ^ std::rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + low + w[i - 7] + high;
    }
    std::array<std::uint64_t, 8> v = state;
    for (std::size_t i = 0; i < 80; ++i) {
        const std::uint64_t sum1 = std::rotr(v[4], 14) ^ std::rotr(v[4], 18) ^ std::rotr(v[4], 41);
        const std::uint64_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint64_t first = v[7] + sum1 + choice + kRounds[i] + w[i];
        const std::uint64_t sum0 = std::rotr(v[0], 28) ^ std::rotr(v[0], 34) ^ std::rotr(v[0], 39);
        const std::uint64_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint64_t second = sum0 + majority;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + first;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = first + second;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        state[i] += v[i];
    }
}

void blake2bBlock(std::array<std::uint64_t, 8>& h, const std::uint8_t* block, std::uint64_t counter,
                  bool last) noexcept
{
    std::array<std::uint64_t, 16> m{};
    for (std::size_t i = 0; i < 16; ++i) {
        std::uint64_t word = 0;
        for (std::size_t k = 8; k-- > 0;) {
            word = (word << 8) | block[8 * i + k];
        }
        m[i] = word;
    }
    std::array<std::uint64_t, 16> v{};
    for (std::size_t i = 0; i < 8; ++i) {
        v[i] = h[i];
        v[i + 8] = kStart[i];
    }
    v[12] ^= counter;
    if (last) {
        v[14] = ~v[14];
    }
    const auto mix = [&v](std::size_t a, std::size_t b, std::size_t c, std::size_t d, std::uint64_t x,
                          std::uint64_t y) {
        v[a] = v[a] + v[b] + x;
        v[d] = std::rotr(v[d] ^ v[a], 32);
        v[c] = v[c] + v[d];
        v[b] = std::rotr(v[b] ^ v[c], 24);
        v[a] = v[a] + v[b] + y;
        v[d] = std::rotr(v[d] ^ v[a], 16);
        v[c] = v[c] + v[d];
        v[b] = std::rotr(v[b] ^ v[c], 63);
    };
    for (std::size_t round = 0; round < 12; ++round) {
        const std::array<std::uint8_t, 16>& s = kSigma[round % 10];
        mix(0, 4, 8, 12, m[s[0]], m[s[1]]);
        mix(1, 5, 9, 13, m[s[2]], m[s[3]]);
        mix(2, 6, 10, 14, m[s[4]], m[s[5]]);
        mix(3, 7, 11, 15, m[s[6]], m[s[7]]);
        mix(0, 5, 10, 15, m[s[8]], m[s[9]]);
        mix(1, 6, 11, 12, m[s[10]], m[s[11]]);
        mix(2, 7, 8, 13, m[s[12]], m[s[13]]);
        mix(3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        h[i] ^= v[i] ^ v[i + 8];
    }
}

void carry(Field& o) noexcept
{
    for (std::size_t i = 0; i < 16; ++i) {
        o[i] += std::int64_t{1} << 16;
        const std::int64_t c = o[i] >> 16;
        if (i < 15) {
            o[i + 1] += c - 1;
        } else {
            o[0] += 38 * (c - 1);
        }
        o[i] -= c * 65536;
    }
}

void swapIf(Field& p, Field& q, std::int64_t bit) noexcept
{
    const std::int64_t mask = ~(bit - 1);
    for (std::size_t i = 0; i < 16; ++i) {
        const std::int64_t t = mask & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void swapIf(Point& p, Point& q, std::int64_t bit) noexcept
{
    for (std::size_t i = 0; i < 4; ++i) {
        swapIf(p[i], q[i], bit);
    }
}

Encoded packed(const Field& n) noexcept
{
    Field t = n;
    Field m{};
    carry(t);
    carry(t);
    carry(t);
    for (int pass = 0; pass < 2; ++pass) {
        m[0] = t[0] - 0xffed;
        for (std::size_t i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const std::int64_t borrow = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        swapIf(t, m, 1 - borrow);
    }
    Encoded out{};
    for (std::size_t i = 0; i < 16; ++i) {
        out[2 * i] = static_cast<std::uint8_t>(t[i] & 0xff);
        out[2 * i + 1] = static_cast<std::uint8_t>((t[i] >> 8) & 0xff);
    }
    return out;
}

bool differs(const Field& a, const Field& b) noexcept
{
    return packed(a) != packed(b);
}

std::uint8_t parity(const Field& a) noexcept
{
    return static_cast<std::uint8_t>(packed(a)[0] & 1);
}

Field unpacked(const std::uint8_t* n) noexcept
{
    Field o{};
    for (std::size_t i = 0; i < 16; ++i) {
        o[i] = n[2 * i] + (static_cast<std::int64_t>(n[2 * i + 1]) << 8);
    }
    o[15] &= 0x7fff;
    return o;
}

Field plus(const Field& a, const Field& b) noexcept
{
    Field o{};
    for (std::size_t i = 0; i < 16; ++i) {
        o[i] = a[i] + b[i];
    }
    return o;
}

Field minus(const Field& a, const Field& b) noexcept
{
    Field o{};
    for (std::size_t i = 0; i < 16; ++i) {
        o[i] = a[i] - b[i];
    }
    return o;
}

Field times(const Field& a, const Field& b) noexcept
{
    std::array<std::int64_t, 31> t{};
    for (std::size_t i = 0; i < 16; ++i) {
        for (std::size_t j = 0; j < 16; ++j) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (std::size_t i = 0; i < 15; ++i) {
        t[i] += 38 * t[i + 16];
    }
    Field o{};
    std::copy_n(t.begin(), 16, o.begin());
    carry(o);
    carry(o);
    return o;
}

Field squared(const Field& a) noexcept
{
    return times(a, a);
}

Field inverted(const Field& value) noexcept
{
    Field c = value;
    for (int bit = 253; bit >= 0; --bit) {
        c = squared(c);
        if (bit != 2 && bit != 4) {
            c = times(c, value);
        }
    }
    return c;
}

Field raisedForRoot(const Field& value) noexcept
{
    Field c = value;
    for (int bit = 250; bit >= 0; --bit) {
        c = squared(c);
        if (bit != 1) {
            c = times(c, value);
        }
    }
    return c;
}

void add(Point& p, const Point& q) noexcept
{
    const Field a = times(minus(p[1], p[0]), minus(q[1], q[0]));
    const Field b = times(plus(p[0], p[1]), plus(q[0], q[1]));
    const Field c = times(times(p[3], q[3]), kD2);
    const Field half = times(p[2], q[2]);
    const Field d = plus(half, half);
    const Field e = minus(b, a);
    const Field f = minus(d, c);
    const Field g = plus(d, c);
    const Field h = plus(b, a);
    p[0] = times(e, f);
    p[1] = times(h, g);
    p[2] = times(g, f);
    p[3] = times(e, h);
}

Encoded encoded(const Point& p) noexcept
{
    const Field zi = inverted(p[2]);
    const Field tx = times(p[0], zi);
    const Field ty = times(p[1], zi);
    Encoded r = packed(ty);
    r[31] = static_cast<std::uint8_t>(r[31] ^ (parity(tx) << 7));
    return r;
}

Point multiplied(Point q, const std::uint8_t* scalar) noexcept
{
    Point p{kZero, kOne, kOne, kZero};
    for (int i = 255; i >= 0; --i) {
        const std::int64_t bit = (scalar[i / 8] >> (i & 7)) & 1;
        swapIf(p, q, bit);
        add(q, p);
        add(p, p);
        swapIf(p, q, bit);
    }
    return p;
}

Point baseMultiplied(const std::uint8_t* scalar) noexcept
{
    return multiplied(Point{kBaseX, kBaseY, kOne, times(kBaseX, kBaseY)}, scalar);
}

Encoded reducedByOrder(std::array<std::int64_t, 64> x) noexcept
{
    for (std::size_t i = 63; i >= 32; --i) {
        std::int64_t carried = 0;
        std::size_t j = i - 32;
        for (; j < i - 12; ++j) {
            x[j] += carried - 16 * x[i] * kOrder[j - (i - 32)];
            carried = (x[j] + 128) >> 8;
            x[j] -= carried * 256;
        }
        x[j] += carried;
        x[i] = 0;
    }
    std::int64_t remainder = 0;
    for (std::size_t j = 0; j < 32; ++j) {
        x[j] += remainder - (x[31] >> 4) * kOrder[j];
        remainder = x[j] >> 8;
        x[j] &= 255;
    }
    for (std::size_t j = 0; j < 32; ++j) {
        x[j] -= remainder * kOrder[j];
    }
    Encoded r{};
    for (std::size_t i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = static_cast<std::uint8_t>(x[i] & 255);
    }
    return r;
}

bool canonical(const std::uint8_t* scalar) noexcept
{
    for (std::size_t i = 32; i-- > 0;) {
        if (scalar[i] < kOrder[i]) {
            return true;
        }
        if (scalar[i] > kOrder[i]) {
            return false;
        }
    }
    return false;
}

std::array<std::uint64_t, 8> blake2bState(std::string_view data, std::uint64_t length) noexcept
{
    std::array<std::uint64_t, 8> h = kStart;
    h[0] ^= 0x01010000 ^ length;
    const std::uint8_t* bytes = bytesOf(data);
    std::size_t offset = 0;
    while (data.size() - offset > 128) {
        blake2bBlock(h, bytes + offset, offset + 128, false);
        offset += 128;
    }
    std::array<std::uint8_t, 128> last{};
    const std::size_t count = data.size() - offset;
    std::copy_n(bytes + offset, count, last.begin());
    blake2bBlock(h, last.data(), offset + count, true);
    return h;
}

Encoded reducedDigest(const Digest512& digest) noexcept
{
    std::array<std::int64_t, 64> wide{};
    for (std::size_t i = 0; i < 64; ++i) {
        wide[i] = digest[i];
    }
    return reducedByOrder(wide);
}

Digest512 clampedScalar(const Ed25519Seed& seed)
{
    Digest512 expanded = sha512(std::string_view(reinterpret_cast<const char*>(seed.data()), seed.size()));
    expanded[0] = static_cast<std::uint8_t>(expanded[0] & 248U);
    expanded[31] = static_cast<std::uint8_t>((expanded[31] & 127U) | 64U);
    return expanded;
}

std::optional<Point> negatedPoint(const Ed25519Key& key) noexcept
{
    Point r{};
    r[2] = kOne;
    r[1] = unpacked(key.data());
    Field num = squared(r[1]);
    Field den = times(num, kD);
    num = minus(num, r[2]);
    den = plus(r[2], den);
    const Field den2 = squared(den);
    const Field den4 = squared(den2);
    const Field den6 = times(den4, den2);
    Field t = times(times(den6, num), den);
    t = raisedForRoot(t);
    t = times(times(times(t, num), den), den);
    r[0] = times(t, den);
    if (differs(times(squared(r[0]), den), num)) {
        r[0] = times(r[0], kRootOfMinusOne);
    }
    if (differs(times(squared(r[0]), den), num)) {
        return std::nullopt;
    }
    if (parity(r[0]) == (key[31] >> 7)) {
        r[0] = minus(kZero, r[0]);
    }
    r[3] = times(r[0], r[1]);
    return r;
}

}

Digest512 sha512(std::string_view data)
{
    std::array<std::uint64_t, 8> state = kStart;
    const std::uint8_t* bytes = bytesOf(data);
    const std::size_t full = data.size() / 128;
    for (std::size_t i = 0; i < full; ++i) {
        sha512Block(state, bytes + 128 * i);
    }
    std::array<std::uint8_t, 256> tail{};
    const std::size_t rest = data.size() - 128 * full;
    std::copy_n(bytes + 128 * full, rest, tail.begin());
    tail[rest] = 0x80;
    const std::size_t length = rest + 1 > 112 ? 256 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    for (std::size_t k = 0; k < 8; ++k) {
        tail[length - 1 - k] = static_cast<std::uint8_t>(bits >> (8 * k));
    }
    for (std::size_t offset = 0; offset < length; offset += 128) {
        sha512Block(state, tail.data() + offset);
    }
    Digest512 digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t k = 0; k < 8; ++k) {
            digest[8 * i + k] = static_cast<std::uint8_t>(state[i] >> (56 - 8 * k));
        }
    }
    return digest;
}

Digest512 blake2b512(std::string_view data)
{
    const std::array<std::uint64_t, 8> h = blake2bState(data, 64);
    Digest512 digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t k = 0; k < 8; ++k) {
            digest[8 * i + k] = static_cast<std::uint8_t>(h[i] >> (8 * k));
        }
    }
    return digest;
}

Digest256 blake2b256(std::string_view data)
{
    const std::array<std::uint64_t, 8> h = blake2bState(data, 32);
    Digest256 digest{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t k = 0; k < 8; ++k) {
            digest[8 * i + k] = static_cast<std::uint8_t>(h[i] >> (8 * k));
        }
    }
    return digest;
}

Ed25519Key ed25519PublicKey(const Ed25519Seed& seed)
{
    const Digest512 scalar = clampedScalar(seed);
    return encoded(baseMultiplied(scalar.data()));
}

Ed25519Signature signEd25519(std::string_view message, const Ed25519Seed& seed)
{
    const Digest512 scalar = clampedScalar(seed);
    const Encoded publicKey = encoded(baseMultiplied(scalar.data()));
    std::string prefixed(reinterpret_cast<const char*>(scalar.data() + 32), 32);
    prefixed.append(message);
    const Encoded nonce = reducedDigest(sha512(prefixed));
    const Encoded committed = encoded(baseMultiplied(nonce.data()));
    std::string hashed(reinterpret_cast<const char*>(committed.data()), committed.size());
    hashed.append(reinterpret_cast<const char*>(publicKey.data()), publicKey.size());
    hashed.append(message);
    const Encoded challenge = reducedDigest(sha512(hashed));
    std::array<std::int64_t, 64> sum{};
    for (std::size_t i = 0; i < 32; ++i) {
        sum[i] = nonce[i];
    }
    for (std::size_t i = 0; i < 32; ++i) {
        for (std::size_t j = 0; j < 32; ++j) {
            sum[i + j] += static_cast<std::int64_t>(challenge[i]) * scalar[j];
        }
    }
    const Encoded response = reducedByOrder(sum);
    Ed25519Signature signature{};
    std::copy(committed.begin(), committed.end(), signature.begin());
    std::copy(response.begin(), response.end(), signature.begin() + 32);
    return signature;
}

bool verifyEd25519(std::string_view message, const Ed25519Signature& signature, const Ed25519Key& publicKey)
{
    if (!canonical(signature.data() + 32)) {
        return false;
    }
    const std::optional<Point> negated = negatedPoint(publicKey);
    if (!negated) {
        return false;
    }
    std::string hashed;
    hashed.reserve(64 + message.size());
    hashed.append(reinterpret_cast<const char*>(signature.data()), 32);
    hashed.append(reinterpret_cast<const char*>(publicKey.data()), publicKey.size());
    hashed.append(message);
    const Digest512 digest = sha512(hashed);
    std::array<std::int64_t, 64> wide{};
    for (std::size_t i = 0; i < 64; ++i) {
        wide[i] = digest[i];
    }
    const Encoded h = reducedByOrder(wide);
    Point p = multiplied(*negated, h.data());
    add(p, baseMultiplied(signature.data() + 32));
    const Encoded r = encoded(p);
    return std::equal(r.begin(), r.end(), signature.begin());
}

}
