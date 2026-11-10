#pragma once

#include <gs/base/ByteOrder.h>
#include <gs/base/Expected.h>
#include <gs/protocol/Constants.h>
#include <gs/protocol/Enums.h>
#include <gs/protocol/Errors.h>
#include <gs/protocol/Messages.h>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace gs::protocol {

using ToServerMessage = std::variant<
    ClientHello, ChannelOpen,
    TouchFrame, Key, Text, Scroll, BackOrScreenOn, UhidCreate, UhidInput, UhidDestroy, UhidFeature,
    ConfigureVideo, SetVideoBitrate, SetMaxFps, RequestKeyFrame, SetVideoPaused, ConfigureAudio,
    SetClipboard, GetClipboard, SetDisplayPower, SetOrientation, StartApp, SystemPanel,
    RequestAppList, RequestAppIcon, RequestScreenshot, SetShowTouches, SubscribeStatus,
    Ping, Goodbye, ReceiverReport,
    CompanionHello, CursorState,
    CommitText, EditorAction, KeyHintLayout, KeyHintState,
    ScreenshotRequest>;

using ToClientMessage = std::variant<
    ServerHello, Welcome, HandshakeError, ChannelAccepted,
    UhidOutput, UhidStatus, ClipboardChanged, ClipboardAck, DisplayChanged, ForegroundAppChanged, DeviceStatus,
    Pong, ServerGoodbye, Notice,
    AppList, AppIcon, Screenshot, RequestFailed,
    CompanionWelcome, OverlayChanged,
    InputBoxChanged,
    ScreenshotChunk, ScreenshotFailed>;

struct FrameHeader {
    std::uint16_t type = 0;
    std::uint32_t length = 0;
};

struct MediaFrameFlags {
    bool keyFrame = false;
    bool discardable = false;
};

struct MediaHeader {
    MediaKind kind = MediaKind::Frame;
    bool keyFrame = false;
    bool discardable = false;
    std::uint16_t session = 0;
    std::int64_t ptsUs = 0;
    std::uint32_t encodeDelayUs = 0;
    std::uint32_t size = 0;
};

struct MediaPacket {
    MediaHeader header;
    std::variant<std::monostate, VideoSessionInfo, AudioSessionInfo> session;
    std::span<const std::uint8_t> payload;
};

[[nodiscard]] bool isValidUtf8(std::span<const std::uint8_t> bytes) noexcept;

[[nodiscard]] constexpr Direction directionOf(std::uint16_t type) noexcept
{
    return (type & 0x8000U) != 0U ? Direction::ToClient : Direction::ToServer;
}

[[nodiscard]] constexpr FrameContext contextOf(std::uint16_t type) noexcept
{
    const auto range = (type >> 8U) & 0x7FU;
    if (range == 0x00U) {
        return FrameContext::Handshake;
    }
    if (range == 0x05U) {
        return FrameContext::Bulk;
    }
    if (range == 0x06U) {
        return FrameContext::Companion;
    }
    return FrameContext::Control;
}

[[nodiscard]] constexpr bool isAllowedIn(FrameContext context, std::uint16_t type) noexcept
{
    if (context != FrameContext::Companion) {
        return contextOf(type) == context;
    }
    const auto base = static_cast<std::uint16_t>(type & 0x7FFFU);
    return contextOf(type) == FrameContext::Companion
           || base == static_cast<std::uint16_t>(MessageType::Ping)
           || base == static_cast<std::uint16_t>(MessageType::Goodbye);
}

namespace detail {

template <bool kCountOnly>
class EncodeVisitor
{
public:
    explicit EncodeVisitor(std::span<std::uint8_t> out) noexcept
        : m_out(out)
    {
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::optional<EncodeError> error() const noexcept { return m_error; }

    void magic(std::uint32_t value) { put(value); }
    void u8(std::string_view, std::uint8_t value) { put(value); }
    void u16(std::string_view, std::uint16_t value) { put(value); }
    void u32(std::string_view, std::uint32_t value) { put(value); }
    void u64(std::string_view, std::uint64_t value) { put(value); }
    void i16(std::string_view, std::int16_t value) { put(std::bit_cast<std::uint16_t>(value)); }
    void i64(std::string_view, std::int64_t value) { put(std::bit_cast<std::uint64_t>(value)); }

    void u8Range(std::string_view, std::uint8_t value, std::uint8_t min, std::uint8_t max)
    {
        putInRange(value, min, max);
    }

    void u16Range(std::string_view, std::uint16_t value, std::uint16_t min,
                  std::uint16_t max)
    {
        putInRange(value, min, max);
    }

    void u32Mask(std::string_view, std::uint32_t value, std::uint32_t allowed)
    {
        if ((value & ~allowed) != 0U) {
            fail(EncodeError::ValueOutOfRange);
            return;
        }
        put(value);
    }

    void boolean(std::string_view, bool value)
    {
        put(static_cast<std::uint8_t>(value ? 1U : 0U));
    }

    template <typename E>
    void enumeration(std::string_view, E value)
    {
        const auto raw = static_cast<std::underlying_type_t<E>>(value);
        if (!isValidEnumValue<E>(raw)) {
            fail(EncodeError::ValueOutOfRange);
            return;
        }
        put(raw);
    }

    template <std::size_t N>
    void bytes(std::string_view, const std::array<std::uint8_t, N>& value)
    {
        putRaw(value);
    }

    void str8(std::string_view, const std::string& value, std::size_t max) { putString<std::uint8_t>(value, max); }
    void str16(std::string_view, const std::string& value, std::size_t max) { putString<std::uint16_t>(value, max); }
    void str32(std::string_view, const std::string& value, std::size_t max) { putString<std::uint32_t>(value, max); }
    void blob8(std::string_view, const Bytes& value, std::size_t max) { putPrefixed<std::uint8_t>(value, max); }
    void blob16(std::string_view, const Bytes& value, std::size_t max) { putPrefixed<std::uint16_t>(value, max); }
    void blob32(std::string_view, const Bytes& value, std::size_t max) { putPrefixed<std::uint32_t>(value, max); }

    void flag8(std::string_view, bool bit0)
    {
        put(static_cast<std::uint8_t>(bit0 ? 1U : 0U));
    }

    void flag8x2(std::string_view, bool bit0, std::string_view, bool bit1)
    {
        put(static_cast<std::uint8_t>((bit0 ? 1U : 0U) | (bit1 ? 2U : 0U)));
    }

    void flag8x3(std::string_view, bool bit0, std::string_view, bool bit1, std::string_view, bool bit2)
    {
        put(static_cast<std::uint8_t>((bit0 ? 1U : 0U) | (bit1 ? 2U : 0U) | (bit2 ? 4U : 0U)));
    }

    template <typename T>
    void object(std::string_view, const T& value)
    {
        T::visit(*this, value);
    }

    template <typename List>
    void list8(std::string_view, const List& items, std::size_t min, std::size_t max)
    {
        putList<std::uint8_t>(items, min, max);
    }

    template <typename List>
    void list16(std::string_view, const List& items, std::size_t min, std::size_t max)
    {
        putList<std::uint16_t>(items, min, max);
    }

private:
    void fail(EncodeError error) noexcept
    {
        if (!m_error) {
            m_error = error;
        }
    }

    template <std::unsigned_integral T>
    void put(T value)
    {
        if (m_error) {
            return;
        }
        if constexpr (!kCountOnly) {
            if (m_out.size() - m_size < sizeof(T)) {
                fail(EncodeError::BufferTooSmall);
                return;
            }
            storeBigEndian<T>(value, m_out.subspan(m_size).template first<sizeof(T)>());
        }
        m_size += sizeof(T);
    }

    template <std::unsigned_integral T>
    void putInRange(T value, T min, T max)
    {
        if (value < min || value > max) {
            fail(EncodeError::ValueOutOfRange);
            return;
        }
        put(value);
    }

    void putRaw(std::span<const std::uint8_t> data)
    {
        if (m_error) {
            return;
        }
        if constexpr (!kCountOnly) {
            if (m_out.size() - m_size < data.size()) {
                fail(EncodeError::BufferTooSmall);
                return;
            }
            std::copy(data.begin(), data.end(), m_out.subspan(m_size).begin());
        }
        m_size += data.size();
    }

    template <std::unsigned_integral Length>
    void putPrefixed(std::span<const std::uint8_t> data, std::size_t max)
    {
        if (data.size() > max) {
            fail(EncodeError::FieldTooLong);
            return;
        }
        put(static_cast<Length>(data.size()));
        putRaw(data);
    }

    template <std::unsigned_integral Length>
    void putString(const std::string& value, std::size_t max)
    {
        const std::span<const std::uint8_t> data{
            reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
        if (!isValidUtf8(data)) {
            fail(EncodeError::MalformedUtf8);
            return;
        }
        putPrefixed<Length>(data, max);
    }

    template <std::unsigned_integral Length, typename List>
    void putList(const List& items, std::size_t min, std::size_t max)
    {
        using T = typename List::value_type;
        if (items.size() > max) {
            fail(EncodeError::FieldTooLong);
            return;
        }
        if (items.size() < min) {
            fail(EncodeError::ValueOutOfRange);
            return;
        }
        put(static_cast<Length>(items.size()));
        for (const T& item : items) {
            T::visit(*this, item);
            if (m_error) {
                return;
            }
        }
    }

    std::span<std::uint8_t> m_out;
    std::size_t m_size = 0;
    std::optional<EncodeError> m_error;
};

class DecodeVisitor
{
public:
    explicit DecodeVisitor(std::span<const std::uint8_t> in) noexcept
        : m_in(in)
    {
    }

    [[nodiscard]] std::optional<DecodeError> error() const noexcept { return m_error; }
    [[nodiscard]] std::size_t remaining() const noexcept { return m_in.size() - m_pos; }

    void magic(std::uint32_t expected)
    {
        std::uint32_t value = 0;
        if (get(value) && value != expected) {
            fail(DecodeError::BadMagic);
        }
    }

    void u8(std::string_view, std::uint8_t& out) { get(out); }
    void u16(std::string_view, std::uint16_t& out) { get(out); }
    void u32(std::string_view, std::uint32_t& out) { get(out); }
    void u64(std::string_view, std::uint64_t& out) { get(out); }

    void i16(std::string_view, std::int16_t& out)
    {
        std::uint16_t raw = 0;
        if (get(raw)) {
            out = std::bit_cast<std::int16_t>(raw);
        }
    }

    void i64(std::string_view, std::int64_t& out)
    {
        std::uint64_t raw = 0;
        if (get(raw)) {
            out = std::bit_cast<std::int64_t>(raw);
        }
    }

    void u8Range(std::string_view, std::uint8_t& out, std::uint8_t min, std::uint8_t max)
    {
        getInRange(out, min, max);
    }

    void u16Range(std::string_view, std::uint16_t& out, std::uint16_t min,
                  std::uint16_t max)
    {
        getInRange(out, min, max);
    }

    void u32Mask(std::string_view, std::uint32_t& out, std::uint32_t allowed)
    {
        if (get(out) && (out & ~allowed) != 0U) {
            fail(DecodeError::ReservedBitsSet);
        }
    }

    void boolean(std::string_view, bool& out)
    {
        std::uint8_t raw = 0;
        if (!get(raw)) {
            return;
        }
        if (raw > 1U) {
            fail(DecodeError::InvalidBool);
            return;
        }
        out = raw == 1U;
    }

    template <typename E>
    void enumeration(std::string_view, E& out)
    {
        std::underlying_type_t<E> raw{};
        if (!get(raw)) {
            return;
        }
        if (!isValidEnumValue<E>(raw)) {
            fail(DecodeError::InvalidEnum);
            return;
        }
        out = static_cast<E>(raw);
    }

    template <std::size_t N>
    void bytes(std::string_view, std::array<std::uint8_t, N>& out)
    {
        if (const auto data = take(N)) {
            std::copy(data->begin(), data->end(), out.begin());
        }
    }

    void str8(std::string_view, std::string& out, std::size_t max) { getString<std::uint8_t>(out, max); }
    void str16(std::string_view, std::string& out, std::size_t max) { getString<std::uint16_t>(out, max); }
    void str32(std::string_view, std::string& out, std::size_t max) { getString<std::uint32_t>(out, max); }
    void blob8(std::string_view, Bytes& out, std::size_t max) { getBlob<std::uint8_t>(out, max); }
    void blob16(std::string_view, Bytes& out, std::size_t max) { getBlob<std::uint16_t>(out, max); }
    void blob32(std::string_view, Bytes& out, std::size_t max) { getBlob<std::uint32_t>(out, max); }

    void flag8(std::string_view, bool& bit0)
    {
        std::uint8_t raw = 0;
        if (!getFlags(raw, 0x01U)) {
            return;
        }
        bit0 = (raw & 0x01U) != 0U;
    }

    void flag8x2(std::string_view, bool& bit0, std::string_view, bool& bit1)
    {
        std::uint8_t raw = 0;
        if (!getFlags(raw, 0x03U)) {
            return;
        }
        bit0 = (raw & 0x01U) != 0U;
        bit1 = (raw & 0x02U) != 0U;
    }

    void flag8x3(std::string_view, bool& bit0, std::string_view, bool& bit1, std::string_view, bool& bit2)
    {
        std::uint8_t raw = 0;
        if (!getFlags(raw, 0x07U)) {
            return;
        }
        bit0 = (raw & 0x01U) != 0U;
        bit1 = (raw & 0x02U) != 0U;
        bit2 = (raw & 0x04U) != 0U;
    }

    template <typename T>
    void object(std::string_view, T& out)
    {
        T::visit(*this, out);
    }

    template <typename List>
    void list8(std::string_view, List& out, std::size_t min, std::size_t max)
    {
        getList<std::uint8_t>(out, min, max);
    }

    template <typename List>
    void list16(std::string_view, List& out, std::size_t min, std::size_t max)
    {
        getList<std::uint16_t>(out, min, max);
    }

private:
    void fail(DecodeError error) noexcept
    {
        if (!m_error) {
            m_error = error;
        }
    }

    template <std::unsigned_integral T>
    bool get(T& out)
    {
        if (m_error) {
            return false;
        }
        if (remaining() < sizeof(T)) {
            fail(DecodeError::Truncated);
            return false;
        }
        out = loadBigEndian<T>(m_in.subspan(m_pos).template first<sizeof(T)>());
        m_pos += sizeof(T);
        return true;
    }

    template <std::unsigned_integral T>
    void getInRange(T& out, T min, T max)
    {
        if (get(out) && (out < min || out > max)) {
            fail(DecodeError::OutOfRange);
        }
    }

    bool getFlags(std::uint8_t& out, std::uint8_t allowed)
    {
        if (!get(out)) {
            return false;
        }
        if ((out & ~allowed) != 0) {
            fail(DecodeError::ReservedBitsSet);
            return false;
        }
        return true;
    }

    std::optional<std::span<const std::uint8_t>> take(std::size_t count)
    {
        if (m_error) {
            return std::nullopt;
        }
        if (remaining() < count) {
            fail(DecodeError::Truncated);
            return std::nullopt;
        }
        const auto data = m_in.subspan(m_pos, count);
        m_pos += count;
        return data;
    }

    template <std::unsigned_integral Length>
    std::optional<std::span<const std::uint8_t>> getPrefixed(std::size_t max)
    {
        Length length = 0;
        if (!get(length)) {
            return std::nullopt;
        }
        if (length > max) {
            fail(DecodeError::LengthExceeded);
            return std::nullopt;
        }
        return take(length);
    }

    template <std::unsigned_integral Length>
    void getString(std::string& out, std::size_t max)
    {
        const auto data = getPrefixed<Length>(max);
        if (!data) {
            return;
        }
        if (!isValidUtf8(*data)) {
            fail(DecodeError::MalformedUtf8);
            return;
        }
        out.assign(reinterpret_cast<const char*>(data->data()), data->size());
    }

    template <std::unsigned_integral Length>
    void getBlob(Bytes& out, std::size_t max)
    {
        if (const auto data = getPrefixed<Length>(max)) {
            out.assign(data->begin(), data->end());
        }
    }

    template <std::unsigned_integral Length, typename List>
    void getList(List& out, std::size_t min, std::size_t max)
    {
        using T = typename List::value_type;
        Length count = 0;
        if (!get(count)) {
            return;
        }
        if (count > max) {
            fail(DecodeError::LengthExceeded);
            return;
        }
        if (count < min) {
            fail(DecodeError::OutOfRange);
            return;
        }
        out.clear();
        if constexpr (requires { out.reserve(std::size_t{}); }) {
            out.reserve(count);
        }
        for (std::size_t i = 0; i < count; ++i) {
            T item{};
            T::visit(*this, item);
            if (m_error) {
                return;
            }
            out.push_back(std::move(item));
        }
    }

    std::span<const std::uint8_t> m_in;
    std::size_t m_pos = 0;
    std::optional<DecodeError> m_error;
};

template <typename M>
concept HasMessageConstraint = requires(const M& message) {
    { M::isValid(message) } -> std::convertible_to<bool>;
};

}

template <typename M>
[[nodiscard]] Expected<std::size_t, EncodeError> encodedPayloadSize(const M& message)
{
    detail::EncodeVisitor<true> counter{std::span<std::uint8_t>{}};
    M::visit(counter, message);
    if (const auto error = counter.error()) {
        return makeUnexpected(*error);
    }
    if constexpr (detail::HasMessageConstraint<M>) {
        if (!M::isValid(message)) {
            return makeUnexpected(EncodeError::ValueOutOfRange);
        }
    }
    return counter.size();
}

template <typename M>
[[nodiscard]] Expected<Bytes, EncodeError> encodePayload(const M& message)
{
    const auto size = encodedPayloadSize(message);
    if (!size) {
        return makeUnexpected(size.error());
    }
    Bytes payload(*size);
    detail::EncodeVisitor<false> writer{payload};
    M::visit(writer, message);
    if (const auto error = writer.error()) {
        return makeUnexpected(*error);
    }
    return payload;
}

template <typename M>
[[nodiscard]] Expected<std::size_t, EncodeError> encodeFrameInto(const M& message,
                                                                 std::span<std::uint8_t> out)
{
    const auto payloadSize = encodedPayloadSize(message);
    if (!payloadSize) {
        return makeUnexpected(payloadSize.error());
    }
    const auto type = static_cast<std::uint16_t>(M::kType);
    if (*payloadSize > payloadLimit(contextOf(type))) {
        return makeUnexpected(EncodeError::FrameTooLarge);
    }
    const std::size_t frameSize = kFrameHeaderSize + *payloadSize;
    if (out.size() < frameSize) {
        return makeUnexpected(EncodeError::BufferTooSmall);
    }
    storeBigEndian<std::uint16_t>(type, out.first<2>());
    storeBigEndian<std::uint32_t>(static_cast<std::uint32_t>(*payloadSize), out.subspan<2, 4>());
    detail::EncodeVisitor<false> writer{out.subspan(kFrameHeaderSize, *payloadSize)};
    M::visit(writer, message);
    if (const auto error = writer.error()) {
        return makeUnexpected(*error);
    }
    return frameSize;
}

template <typename M>
[[nodiscard]] Expected<Bytes, EncodeError> encodeFrame(const M& message)
{
    const auto payloadSize = encodedPayloadSize(message);
    if (!payloadSize) {
        return makeUnexpected(payloadSize.error());
    }
    Bytes frame(kFrameHeaderSize + *payloadSize);
    const auto written = encodeFrameInto(message, frame);
    if (!written) {
        return makeUnexpected(written.error());
    }
    return frame;
}

[[nodiscard]] Expected<Bytes, EncodeError> encodeVideoSession(std::uint16_t session,
                                                              const VideoSessionInfo& info);
[[nodiscard]] Expected<Bytes, EncodeError> encodeAudioSession(std::uint16_t session,
                                                              const AudioSessionInfo& info);
[[nodiscard]] Expected<Bytes, EncodeError> encodeMediaConfig(MediaChannel channel,
                                                             std::uint16_t session,
                                                             std::span<const std::uint8_t> payload);
[[nodiscard]] Expected<Bytes, EncodeError> encodeMediaFrame(MediaChannel channel,
                                                            std::uint16_t session,
                                                            MediaFrameFlags flags,
                                                            std::int64_t ptsUs,
                                                            std::uint32_t encodeDelayUs,
                                                            std::span<const std::uint8_t> payload);

template <typename M>
[[nodiscard]] Expected<M, DecodeError> decodePayload(std::span<const std::uint8_t> payload)
{
    detail::DecodeVisitor reader{payload};
    M message{};
    M::visit(reader, message);
    if (const auto error = reader.error()) {
        return makeUnexpected(*error);
    }
    if (reader.remaining() != 0) {
        return makeUnexpected(DecodeError::TrailingBytes);
    }
    if constexpr (detail::HasMessageConstraint<M>) {
        if (!M::isValid(message)) {
            return makeUnexpected(DecodeError::OutOfRange);
        }
    }
    return message;
}

[[nodiscard]] bool isKnownMessageType(std::uint16_t type) noexcept;

[[nodiscard]] Expected<FrameHeader, DecodeError> decodeFrameHeader(
    FrameContext context, Direction direction, std::span<const std::uint8_t> bytes);

[[nodiscard]] Expected<ToServerMessage, DecodeError> decodeToServerPayload(
    std::uint16_t type, std::span<const std::uint8_t> payload);
[[nodiscard]] Expected<ToClientMessage, DecodeError> decodeToClientPayload(
    std::uint16_t type, std::span<const std::uint8_t> payload);

[[nodiscard]] Expected<ToServerMessage, DecodeError> decodeToServerFrame(
    FrameContext context, std::span<const std::uint8_t> frame);
[[nodiscard]] Expected<ToClientMessage, DecodeError> decodeToClientFrame(
    FrameContext context, std::span<const std::uint8_t> frame);

[[nodiscard]] Expected<MediaHeader, DecodeError> decodeMediaHeader(
    MediaChannel channel, std::span<const std::uint8_t> bytes);

[[nodiscard]] Expected<MediaPacket, DecodeError> decodeMediaPacket(
    MediaChannel channel, std::span<const std::uint8_t> packet);

}
