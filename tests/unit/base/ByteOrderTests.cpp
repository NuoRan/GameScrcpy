#include "gs/base/ByteOrder.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>

namespace {

template <typename T>
constexpr T roundTrip(T value)
{
    std::array<std::uint8_t, sizeof(T)> buffer{};
    gs::storeBigEndian<T>(value, buffer);
    return gs::loadBigEndian<T>(buffer);
}

static_assert(roundTrip<std::uint8_t>(0xABU) == 0xABU);
static_assert(roundTrip<std::uint16_t>(0xBEEFU) == 0xBEEFU);
static_assert(roundTrip<std::uint32_t>(0xDEADBEEFU) == 0xDEADBEEFU);
static_assert(roundTrip<std::uint64_t>(0x0123456789ABCDEFULL) == 0x0123456789ABCDEFULL);

}

TEST_CASE("storeBigEndian writes the most significant byte first")
{
    std::array<std::uint8_t, 4> buffer{};
    gs::storeBigEndian<std::uint32_t>(0x11223344U, buffer);
    CHECK(buffer == std::array<std::uint8_t, 4>{0x11, 0x22, 0x33, 0x44});
}

TEST_CASE("loadBigEndian reads the most significant byte first")
{
    const std::array<std::uint8_t, 8> buffer{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    CHECK(gs::loadBigEndian<std::uint64_t>(buffer) == 0x0123456789ABCDEFULL);
}

TEST_CASE("big-endian helpers round-trip the extremes of every width")
{
    CHECK(roundTrip(std::numeric_limits<std::uint16_t>::max()) ==
          std::numeric_limits<std::uint16_t>::max());
    CHECK(roundTrip(std::numeric_limits<std::uint32_t>::max()) ==
          std::numeric_limits<std::uint32_t>::max());
    CHECK(roundTrip(std::numeric_limits<std::uint64_t>::max()) ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK(roundTrip<std::uint64_t>(0) == 0);
}
