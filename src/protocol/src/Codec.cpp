#include "gs/protocol/Codec.h"

namespace gs::protocol {

namespace {

template <typename Variant>
struct VariantCodec;

template <typename... Ms>
struct VariantCodec<std::variant<Ms...>> {
    using Message = std::variant<Ms...>;
    using Result = Expected<Message, DecodeError>;

    static constexpr bool contains(std::uint16_t type) noexcept
    {
        return ((static_cast<std::uint16_t>(Ms::kType) == type) || ...);
    }

    static Result decode(std::uint16_t type, std::span<const std::uint8_t> payload)
    {
        Result result = makeUnexpected(DecodeError::UnknownType);
        const bool matched = ((static_cast<std::uint16_t>(Ms::kType) == type
                                   ? (result = wrap<Ms>(decodePayload<Ms>(payload)), true)
                                   : false) ||
                              ...);
        static_cast<void>(matched);
        return result;
    }

private:
    template <typename M>
    static Result wrap(Expected<M, DecodeError>&& decoded)
    {
        if (!decoded) {
            return makeUnexpected(decoded.error());
        }
        return Message{std::in_place_type<M>, std::move(*decoded)};
    }
};

using ToServerCodec = VariantCodec<ToServerMessage>;
using ToClientCodec = VariantCodec<ToClientMessage>;

template <typename Codec>
typename Codec::Result decodeFrame(FrameContext context, Direction direction,
                                   std::span<const std::uint8_t> frame)
{
    const auto header = decodeFrameHeader(context, direction, frame);
    if (!header) {
        return makeUnexpected(header.error());
    }
    const auto payload = frame.subspan(kFrameHeaderSize);
    if (payload.size() < header->length) {
        return makeUnexpected(DecodeError::Truncated);
    }
    return Codec::decode(header->type, payload.first(header->length));
}

Expected<Bytes, EncodeError> packMedia(MediaChannel channel, MediaKind kind, MediaFrameFlags flags,
                                       std::uint16_t session, std::int64_t ptsUs, std::uint32_t encodeDelayUs,
                                       std::span<const std::uint8_t> payload)
{
    const bool hasFlags = flags.keyFrame || flags.discardable;
    if (session == 0 || (kind != MediaKind::Frame && (hasFlags || ptsUs != 0 || encodeDelayUs != 0))) {
        return makeUnexpected(EncodeError::ValueOutOfRange);
    }
    if (payload.size() > payloadLimit(channel)) {
        return makeUnexpected(EncodeError::FrameTooLarge);
    }
    Bytes packet(kMediaHeaderSize + payload.size());
    const std::span<std::uint8_t> out{packet};
    out[0] = static_cast<std::uint8_t>(kind);
    out[1] = static_cast<std::uint8_t>((flags.keyFrame ? 0x01U : 0x00U) |
                                       (flags.discardable ? 0x02U : 0x00U));
    storeBigEndian<std::uint16_t>(session, out.subspan<2, 2>());
    storeBigEndian<std::uint64_t>(std::bit_cast<std::uint64_t>(ptsUs), out.subspan<4, 8>());
    storeBigEndian<std::uint32_t>(encodeDelayUs, out.subspan<12, 4>());
    storeBigEndian<std::uint32_t>(static_cast<std::uint32_t>(payload.size()), out.subspan<16, 4>());
    std::copy(payload.begin(), payload.end(), out.subspan(kMediaHeaderSize).begin());
    return packet;
}

template <typename Info>
Expected<Bytes, EncodeError> packSession(MediaChannel channel, std::uint16_t session,
                                         const Info& info)
{
    const auto payload = encodePayload(info);
    if (!payload) {
        return makeUnexpected(payload.error());
    }
    return packMedia(channel, MediaKind::Session, {}, session, 0, 0, *payload);
}

}

bool isKnownMessageType(std::uint16_t type) noexcept
{
    return ToServerCodec::contains(type) || ToClientCodec::contains(type);
}

Expected<FrameHeader, DecodeError> decodeFrameHeader(FrameContext context, Direction direction,
                                                     std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kFrameHeaderSize) {
        return makeUnexpected(DecodeError::Truncated);
    }
    const FrameHeader header{
        .type = loadBigEndian<std::uint16_t>(bytes.first<2>()),
        .length = loadBigEndian<std::uint32_t>(bytes.subspan<2, 4>()),
    };
    if (!isKnownMessageType(header.type)) {
        return makeUnexpected(DecodeError::UnknownType);
    }
    if (directionOf(header.type) != direction) {
        return makeUnexpected(DecodeError::WrongDirection);
    }
    if (!isAllowedIn(context, header.type)) {
        return makeUnexpected(DecodeError::UnexpectedType);
    }
    if (header.length > payloadLimit(context)) {
        return makeUnexpected(DecodeError::FrameTooLarge);
    }
    return header;
}

Expected<ToServerMessage, DecodeError> decodeToServerPayload(std::uint16_t type,
                                                             std::span<const std::uint8_t> payload)
{
    return ToServerCodec::decode(type, payload);
}

Expected<ToClientMessage, DecodeError> decodeToClientPayload(std::uint16_t type,
                                                             std::span<const std::uint8_t> payload)
{
    return ToClientCodec::decode(type, payload);
}

Expected<ToServerMessage, DecodeError> decodeToServerFrame(FrameContext context,
                                                           std::span<const std::uint8_t> frame)
{
    return decodeFrame<ToServerCodec>(context, Direction::ToServer, frame);
}

Expected<ToClientMessage, DecodeError> decodeToClientFrame(FrameContext context,
                                                           std::span<const std::uint8_t> frame)
{
    return decodeFrame<ToClientCodec>(context, Direction::ToClient, frame);
}

Expected<MediaHeader, DecodeError> decodeMediaHeader(MediaChannel channel,
                                                     std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kMediaHeaderSize) {
        return makeUnexpected(DecodeError::Truncated);
    }
    const std::uint8_t rawKind = bytes[0];
    if (!isValidEnumValue<MediaKind>(rawKind)) {
        return makeUnexpected(DecodeError::InvalidEnum);
    }
    const auto kind = static_cast<MediaKind>(rawKind);
    const std::uint8_t flags = bytes[1];
    const std::uint8_t allowedFlags = kind == MediaKind::Frame ? 0x03U : 0x00U;
    if ((flags & ~allowedFlags) != 0) {
        return makeUnexpected(DecodeError::ReservedBitsSet);
    }
    const MediaHeader header{
        .kind = kind,
        .keyFrame = (flags & 0x01U) != 0U,
        .discardable = (flags & 0x02U) != 0U,
        .session = loadBigEndian<std::uint16_t>(bytes.subspan<2, 2>()),
        .ptsUs = std::bit_cast<std::int64_t>(loadBigEndian<std::uint64_t>(bytes.subspan<4, 8>())),
        .encodeDelayUs = loadBigEndian<std::uint32_t>(bytes.subspan<12, 4>()),
        .size = loadBigEndian<std::uint32_t>(bytes.subspan<16, 4>()),
    };
    if (header.session == 0) {
        return makeUnexpected(DecodeError::OutOfRange);
    }
    if (kind != MediaKind::Frame && (header.ptsUs != 0 || header.encodeDelayUs != 0)) {
        return makeUnexpected(DecodeError::OutOfRange);
    }
    if (header.size > payloadLimit(channel)) {
        return makeUnexpected(DecodeError::FrameTooLarge);
    }
    return header;
}

Expected<MediaPacket, DecodeError> decodeMediaPacket(MediaChannel channel,
                                                     std::span<const std::uint8_t> packet)
{
    const auto header = decodeMediaHeader(channel, packet);
    if (!header) {
        return makeUnexpected(header.error());
    }
    const auto rest = packet.subspan(kMediaHeaderSize);
    if (rest.size() < header->size) {
        return makeUnexpected(DecodeError::Truncated);
    }
    MediaPacket result{.header = *header, .session = std::monostate{},
                       .payload = rest.first(header->size)};
    if (header->kind == MediaKind::Session) {
        if (channel == MediaChannel::Video) {
            auto info = decodePayload<VideoSessionInfo>(result.payload);
            if (!info) {
                return makeUnexpected(info.error());
            }
            result.session = *info;
        } else {
            auto info = decodePayload<AudioSessionInfo>(result.payload);
            if (!info) {
                return makeUnexpected(info.error());
            }
            result.session = *info;
        }
    }
    return result;
}

Expected<Bytes, EncodeError> encodeVideoSession(std::uint16_t session, const VideoSessionInfo& info)
{
    return packSession(MediaChannel::Video, session, info);
}

Expected<Bytes, EncodeError> encodeAudioSession(std::uint16_t session, const AudioSessionInfo& info)
{
    return packSession(MediaChannel::Audio, session, info);
}

Expected<Bytes, EncodeError> encodeMediaConfig(MediaChannel channel, std::uint16_t session,
                                               std::span<const std::uint8_t> payload)
{
    return packMedia(channel, MediaKind::Config, {}, session, 0, 0, payload);
}

Expected<Bytes, EncodeError> encodeMediaFrame(MediaChannel channel, std::uint16_t session,
                                              MediaFrameFlags flags, std::int64_t ptsUs,
                                              std::uint32_t encodeDelayUs,
                                              std::span<const std::uint8_t> payload)
{
    return packMedia(channel, MediaKind::Frame, flags, session, ptsUs, encodeDelayUs, payload);
}

}
