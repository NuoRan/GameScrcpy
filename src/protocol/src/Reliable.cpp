#include <gs/protocol/Reliable.h>

#include <gs/base/ByteOrder.h>

#include <algorithm>

namespace gs::protocol {

std::optional<Segment> SegmentReader::next() noexcept
{
    if (m_left == 0) {
        return std::nullopt;
    }
    --m_left;
    const auto seq = loadBigEndian<std::uint32_t>(m_rest.first<4>());
    const auto length = loadBigEndian<std::uint16_t>(m_rest.subspan<4, 2>());
    const Segment segment{.seq = seq, .bytes = m_rest.subspan(kSegmentHeaderSize, length)};
    m_rest = m_rest.subspan(kSegmentHeaderSize + length);
    return segment;
}

Expected<ReliableBlock, DecodeError> decodeReliableBlock(std::span<const std::uint8_t> bytes) noexcept
{
    if (bytes.size() < kAckBlockSize) {
        return makeUnexpected(DecodeError::Truncated);
    }
    ReliableBlock block;
    block.ack = loadBigEndian<std::uint32_t>(bytes.first<4>());
    block.sack = loadBigEndian<std::uint32_t>(bytes.subspan<4, 4>());
    block.count = bytes[8];
    const auto segments = bytes.subspan(kAckBlockSize);
    std::size_t at = 0;
    for (std::uint8_t i = 0; i < block.count; ++i) {
        if (segments.size() - at < kSegmentHeaderSize) {
            return makeUnexpected(DecodeError::Truncated);
        }
        const auto length = loadBigEndian<std::uint16_t>(segments.subspan(at + 4).first<2>());
        if (length == 0) {
            return makeUnexpected(DecodeError::OutOfRange);
        }
        if (segments.size() - at - kSegmentHeaderSize < length) {
            return makeUnexpected(DecodeError::Truncated);
        }
        at += kSegmentHeaderSize + length;
    }
    if (at != segments.size()) {
        return makeUnexpected(DecodeError::TrailingBytes);
    }
    block.segments = segments;
    return block;
}

Expected<ControlParts, DecodeError> splitControlDatagram(std::span<const std::uint8_t> datagram) noexcept
{
    if (datagram.size() < kControlOverhead) {
        return makeUnexpected(DecodeError::Truncated);
    }
    const std::size_t tagAt = datagram.size() - kControlTagSize;
    return ControlParts{.authenticated = datagram.first(tagAt), .tag = datagram.subspan(tagAt)};
}

Expected<ControlBody, DecodeError> decodeControlBody(std::span<const std::uint8_t> authenticated) noexcept
{
    if (authenticated.size() < kDatagramHeaderSize + kCounterSize + kAckBlockSize) {
        return makeUnexpected(DecodeError::Truncated);
    }
    ControlBody body;
    body.counter = loadBigEndian<std::uint64_t>(authenticated.subspan<kDatagramHeaderSize, kCounterSize>());
    if (body.counter == 0) {
        return makeUnexpected(DecodeError::OutOfRange);
    }
    auto block = decodeReliableBlock(authenticated.subspan(kDatagramHeaderSize + kCounterSize));
    if (!block) {
        return makeUnexpected(block.error());
    }
    body.block = *block;
    return body;
}

Expected<ReliableBlock, DecodeError> decodeBulkDatagram(std::span<const std::uint8_t> datagram) noexcept
{
    if (datagram.size() < kBulkOverhead) {
        return makeUnexpected(DecodeError::Truncated);
    }
    return decodeReliableBlock(datagram.subspan(kDatagramHeaderSize));
}

namespace {

void writeAcks(std::span<std::uint8_t> out, std::uint32_t ack, std::uint32_t sack) noexcept
{
    storeBigEndian<std::uint32_t>(ack, out.first<4>());
    storeBigEndian<std::uint32_t>(sack, out.subspan<4, 4>());
}

}

ReliableWriter ReliableWriter::control(std::span<std::uint8_t> out, std::uint32_t connection, std::uint64_t counter,
                                       std::uint32_t ack, std::uint32_t sack) noexcept
{
    encodeDatagramHeader({.channel = kControlChannel, .connection = connection}, out.first<kDatagramHeaderSize>());
    storeBigEndian<std::uint64_t>(counter, out.subspan<kDatagramHeaderSize, kCounterSize>());
    writeAcks(out.subspan(kDatagramHeaderSize + kCounterSize), ack, sack);
    return ReliableWriter(out, kDatagramHeaderSize + kCounterSize + 8, kControlTagSize);
}

ReliableWriter ReliableWriter::bulk(std::span<std::uint8_t> out, std::uint32_t connection, std::uint32_t ack,
                                    std::uint32_t sack) noexcept
{
    encodeDatagramHeader({.channel = kBulkChannel, .connection = connection}, out.first<kDatagramHeaderSize>());
    writeAcks(out.subspan(kDatagramHeaderSize), ack, sack);
    return ReliableWriter(out, kDatagramHeaderSize + 8, 0);
}

std::size_t ReliableWriter::room() const noexcept
{
    const std::size_t used = m_size + m_reserved + kSegmentHeaderSize;
    if (m_count == 255 || used >= kReliableDatagramLimit) {
        return 0;
    }
    return kReliableDatagramLimit - used;
}

bool ReliableWriter::add(std::uint32_t seq, std::span<const std::uint8_t> bytes) noexcept
{
    if (bytes.empty() || bytes.size() > room()) {
        return false;
    }
    storeBigEndian<std::uint32_t>(seq, m_out.subspan(m_size).first<4>());
    storeBigEndian<std::uint16_t>(static_cast<std::uint16_t>(bytes.size()), m_out.subspan(m_size + 4).first<2>());
    std::ranges::copy(bytes, m_out.begin() + static_cast<std::ptrdiff_t>(m_size + kSegmentHeaderSize));
    m_size += kSegmentHeaderSize + bytes.size();
    ++m_count;
    return true;
}

std::span<const std::uint8_t> ReliableWriter::finish() noexcept
{
    m_out[m_countAt] = m_count;
    return m_out.first(m_size);
}

std::span<const std::uint8_t> ReliableWriter::seal(std::span<const std::uint8_t, kControlTagSize> tag) noexcept
{
    std::ranges::copy(tag, m_out.begin() + static_cast<std::ptrdiff_t>(m_size));
    return m_out.first(m_size + kControlTagSize);
}

}
