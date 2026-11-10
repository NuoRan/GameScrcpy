#include <gs/protocol/Datagram.h>

#include <gs/base/ByteOrder.h>

namespace gs::protocol {

void encodeDatagramHeader(const DatagramHeader& header, std::span<std::uint8_t, kDatagramHeaderSize> out) noexcept
{
    storeBigEndian<std::uint16_t>(kDatagramMagic, out.first<2>());
    out[2] = header.channel;
    out[3] = 0;
    storeBigEndian<std::uint32_t>(header.connection, out.subspan<4, 4>());
}

std::array<std::uint8_t, kDatagramHeaderSize> encodeDatagramHeader(const DatagramHeader& header) noexcept
{
    std::array<std::uint8_t, kDatagramHeaderSize> out{};
    encodeDatagramHeader(header, out);
    return out;
}

Expected<DatagramHeader, DecodeError> decodeDatagramHeader(std::span<const std::uint8_t> datagram) noexcept
{
    if (datagram.size() < kDatagramHeaderSize) {
        return makeUnexpected(DecodeError::Truncated);
    }
    if (loadBigEndian<std::uint16_t>(datagram.first<2>()) != kDatagramMagic) {
        return makeUnexpected(DecodeError::BadMagic);
    }
    const std::uint8_t channel = datagram[2];
    if (!isDatagramChannel(channel)) {
        return makeUnexpected(DecodeError::InvalidEnum);
    }
    if (datagram[3] != 0) {
        return makeUnexpected(DecodeError::ReservedBitsSet);
    }
    if (datagram.size() > kDatagramMaxSize) {
        return makeUnexpected(DecodeError::FrameTooLarge);
    }
    return DatagramHeader{.channel = channel, .connection = loadBigEndian<std::uint32_t>(datagram.subspan<4, 4>())};
}

}
