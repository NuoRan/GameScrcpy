#include "gs/record/MediaFile.h"

#include <gs/base/ByteOrder.h>
#include <gs/protocol/Constants.h>

#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <system_error>

namespace gs::record {

std::vector<std::uint8_t> packetBytes(const protocol::MediaPacket& packet)
{
    std::vector<std::uint8_t> bytes(protocol::kMediaHeaderSize + packet.payload.size());
    const std::span<std::uint8_t> out{bytes};
    const protocol::MediaHeader& header = packet.header;
    out[0] = static_cast<std::uint8_t>(header.kind);
    out[1] = static_cast<std::uint8_t>((header.keyFrame ? 0x01U : 0x00U) | (header.discardable ? 0x02U : 0x00U));
    storeBigEndian<std::uint16_t>(header.session, out.subspan<2, 2>());
    storeBigEndian<std::uint64_t>(std::bit_cast<std::uint64_t>(header.ptsUs), out.subspan<4, 8>());
    storeBigEndian<std::uint32_t>(header.encodeDelayUs, out.subspan<12, 4>());
    storeBigEndian<std::uint32_t>(static_cast<std::uint32_t>(packet.payload.size()), out.subspan<16, 4>());
    std::ranges::copy(packet.payload, out.subspan(protocol::kMediaHeaderSize).begin());
    return bytes;
}

bool isRecordedAac(const protocol::AudioSessionInfo& info) noexcept
{
    return static_cast<std::uint8_t>(info.codec) == kRecordedAac;
}

std::vector<std::uint8_t> aacSessionPayload(std::uint32_t sampleRate, std::uint8_t channels)
{
    std::vector<std::uint8_t> payload(kAudioSessionSize, 0);
    const std::span<std::uint8_t> out{payload};
    out[0] = kRecordedAac;
    storeBigEndian<std::uint32_t>(sampleRate, out.subspan<1, 4>());
    out[5] = channels;
    return payload;
}

protocol::AudioSessionInfo aacSessionInfo(std::uint32_t sampleRate, std::uint8_t channels) noexcept
{
    protocol::AudioSessionInfo info;
    info.codec = static_cast<protocol::AudioCodec>(kRecordedAac);
    info.sampleRate = sampleRate;
    info.channels = channels;
    return info;
}

std::optional<protocol::MediaPacket> decodeRecorded(protocol::MediaChannel channel, std::span<const std::uint8_t> bytes)
{
    if (auto packet = protocol::decodeMediaPacket(channel, bytes)) {
        return *packet;
    }
    if (channel != protocol::MediaChannel::Audio) {
        return std::nullopt;
    }
    const auto header = protocol::decodeMediaHeader(channel, bytes);
    if (!header || header->kind != protocol::MediaKind::Session || header->size != kAudioSessionSize ||
        bytes.size() < protocol::kMediaHeaderSize + kAudioSessionSize) {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> payload = bytes.subspan(protocol::kMediaHeaderSize, kAudioSessionSize);
    if (payload[0] != kRecordedAac) {
        return std::nullopt;
    }
    const std::uint32_t sampleRate = loadBigEndian<std::uint32_t>(payload.subspan<1, 4>());
    if (sampleRate == 0 || payload[5] == 0) {
        return std::nullopt;
    }
    return protocol::MediaPacket{.header = *header, .session = aacSessionInfo(sampleRate, payload[5]), .payload = payload};
}

void MediaHead::see(const protocol::MediaPacket& packet)
{
    switch (packet.header.kind) {
    case protocol::MediaKind::Session:
        m_session = packetBytes(packet);
        m_config.clear();
        break;
    case protocol::MediaKind::Config:
        if (!m_session.empty()) {
            m_config = packetBytes(packet);
        }
        break;
    case protocol::MediaKind::Frame:
        break;
    }
}

void MediaHead::clear() noexcept
{
    m_session.clear();
    m_config.clear();
}

std::vector<protocol::MediaPacket> MediaHead::packets(protocol::MediaChannel channel) const
{
    std::vector<protocol::MediaPacket> packets;
    for (const std::vector<std::uint8_t>* bytes : {&m_session, &m_config}) {
        if (bytes->empty()) {
            continue;
        }
        if (auto packet = decodeRecorded(channel, *bytes)) {
            packets.push_back(*packet);
        }
    }
    return packets;
}

MediaReader::MediaReader(const std::filesystem::path& file, protocol::MediaChannel channel)
    : m_file(file, std::ios::binary)
    , m_channel(channel)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    m_opened = m_file.is_open() && !error;
    m_size = m_opened ? static_cast<std::uint64_t>(size) : 0;
}

std::optional<protocol::MediaHeader> MediaReader::peek()
{
    if (m_peeked) {
        return m_peeked;
    }
    if (!m_opened || m_truncated || m_position >= m_size) {
        return std::nullopt;
    }
    if (m_size - m_position < protocol::kMediaHeaderSize) {
        m_truncated = true;
        return std::nullopt;
    }
    m_buffer.resize(protocol::kMediaHeaderSize);
    m_file.clear();
    m_file.seekg(static_cast<std::streamoff>(m_position));
    m_file.read(reinterpret_cast<char*>(m_buffer.data()), static_cast<std::streamsize>(m_buffer.size()));
    if (!m_file) {
        m_truncated = true;
        return std::nullopt;
    }
    const auto header = protocol::decodeMediaHeader(m_channel, m_buffer);
    if (!header || m_size - m_position - protocol::kMediaHeaderSize < header->size) {
        m_truncated = true;
        return std::nullopt;
    }
    m_peeked = *header;
    return m_peeked;
}

std::optional<protocol::MediaPacket> MediaReader::next()
{
    const std::optional<protocol::MediaHeader> header = peek();
    if (!header) {
        return std::nullopt;
    }
    m_peeked.reset();
    m_buffer.resize(protocol::kMediaHeaderSize + header->size);
    m_file.read(reinterpret_cast<char*>(m_buffer.data() + protocol::kMediaHeaderSize),
                static_cast<std::streamsize>(header->size));
    if (!m_file) {
        m_truncated = true;
        return std::nullopt;
    }
    auto packet = decodeRecorded(m_channel, m_buffer);
    if (!packet) {
        m_truncated = true;
        return std::nullopt;
    }
    m_position += protocol::kMediaHeaderSize + header->size;
    return *packet;
}

void MediaReader::skip()
{
    if (const std::optional<protocol::MediaHeader> header = peek()) {
        m_position += protocol::kMediaHeaderSize + header->size;
        m_peeked.reset();
    }
}

void MediaReader::seek(std::uint64_t position)
{
    m_position = std::min(position, m_size);
    m_peeked.reset();
    m_truncated = false;
}

}
