#pragma once

#include <gs/base/Expected.h>
#include <gs/protocol/Datagram.h>
#include <gs/protocol/Errors.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gs::protocol {

inline constexpr std::size_t kReliableDatagramLimit = 1200;
inline constexpr std::size_t kCounterSize = 8;
inline constexpr std::size_t kAckBlockSize = 9;
inline constexpr std::size_t kSegmentHeaderSize = 6;
inline constexpr std::size_t kControlOverhead = kDatagramHeaderSize + kCounterSize + kAckBlockSize + kControlTagSize;
inline constexpr std::size_t kBulkOverhead = kDatagramHeaderSize + kAckBlockSize;
inline constexpr std::size_t kMaxControlSegment = kReliableDatagramLimit - kControlOverhead - kSegmentHeaderSize;
inline constexpr std::size_t kMaxBulkSegment = kReliableDatagramLimit - kBulkOverhead - kSegmentHeaderSize;
inline constexpr std::uint8_t kControlChannel = 0;
inline constexpr std::uint8_t kBulkChannel = 3;

struct Segment {
    std::uint32_t seq = 0;
    std::span<const std::uint8_t> bytes;
};

struct ReliableBlock {
    std::uint32_t ack = 0;
    std::uint32_t sack = 0;
    std::uint8_t count = 0;
    std::span<const std::uint8_t> segments;
};

class SegmentReader
{
public:
    explicit SegmentReader(const ReliableBlock& block) noexcept : m_rest(block.segments), m_left(block.count) {}

    [[nodiscard]] std::optional<Segment> next() noexcept;

private:
    std::span<const std::uint8_t> m_rest;
    std::uint8_t m_left;
};

[[nodiscard]] Expected<ReliableBlock, DecodeError> decodeReliableBlock(std::span<const std::uint8_t> bytes) noexcept;

struct ControlParts {
    std::span<const std::uint8_t> authenticated;
    std::span<const std::uint8_t> tag;
};
[[nodiscard]] Expected<ControlParts, DecodeError> splitControlDatagram(std::span<const std::uint8_t> datagram) noexcept;

struct ControlBody {
    std::uint64_t counter = 0;
    ReliableBlock block;
};
[[nodiscard]] Expected<ControlBody, DecodeError> decodeControlBody(std::span<const std::uint8_t> authenticated) noexcept;

[[nodiscard]] Expected<ReliableBlock, DecodeError> decodeBulkDatagram(std::span<const std::uint8_t> datagram) noexcept;

class ReliableWriter
{
public:
    [[nodiscard]] static ReliableWriter control(std::span<std::uint8_t> out, std::uint32_t connection,
                                                std::uint64_t counter, std::uint32_t ack, std::uint32_t sack) noexcept;
    [[nodiscard]] static ReliableWriter bulk(std::span<std::uint8_t> out, std::uint32_t connection, std::uint32_t ack,
                                             std::uint32_t sack) noexcept;

    [[nodiscard]] std::size_t room() const noexcept;
    [[nodiscard]] bool add(std::uint32_t seq, std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] std::uint8_t count() const noexcept { return m_count; }

    [[nodiscard]] std::span<const std::uint8_t> finish() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> seal(std::span<const std::uint8_t, kControlTagSize> tag) noexcept;

private:
    ReliableWriter(std::span<std::uint8_t> out, std::size_t countAt, std::size_t reserved) noexcept
        : m_out(out), m_countAt(countAt), m_size(countAt + 1), m_reserved(reserved)
    {
    }

    std::span<std::uint8_t> m_out;
    std::size_t m_countAt;
    std::size_t m_size;
    std::size_t m_reserved;
    std::uint8_t m_count = 0;
};

}
