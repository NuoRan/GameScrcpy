#pragma once

#include "gs/base/Expected.h"
#include "gs/platform/TcpStream.h"
#include "gs/protocol/Codec.h"
#include "gs/session/StreamHost.h"
#include "gs/session/Transport.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace gs::session::detail {

inline platform::TcpStream& streamOf(platform::TcpStream& stream) noexcept
{
    return stream;
}

inline ByteStream& streamOf(const std::unique_ptr<ByteStream>& stream) noexcept
{
    return *stream;
}

inline ChannelFailure lostFailure(const platform::TcpStream& stream)
{
    return {stream.readTimedOut() ? ChannelError::TimedOut : ChannelError::Closed};
}

inline ChannelFailure lostFailure(const ByteStream& stream)
{
    return {stream.readTimedOut() ? ChannelError::TimedOut : ChannelError::Closed, protocol::DecodeError::Truncated,
            stream.closedReason()};
}

inline ChannelFailure closedFailure(const platform::TcpStream&)
{
    return {ChannelError::Closed};
}

inline ChannelFailure closedFailure(const ByteStream& stream)
{
    return {ChannelError::Closed, protocol::DecodeError::Truncated, stream.closedReason()};
}

template <typename Stream>
class StreamFrameChannel final : public FrameChannel
{
public:
    StreamFrameChannel(Stream stream, protocol::FrameContext context) noexcept
        : m_stream(std::move(stream)), m_context(context)
    {
    }

    Expected<std::span<const std::uint8_t>, ChannelFailure> receive() override
    {
        auto& stream = streamOf(m_stream);
        m_buffer.resize(protocol::kFrameHeaderSize);
        if (!stream.readExact(m_buffer)) {
            return makeUnexpected(lostFailure(stream));
        }
        const auto header = protocol::decodeFrameHeader(m_context, protocol::Direction::ToClient, m_buffer);
        if (!header) {
            return makeUnexpected(ChannelFailure{ChannelError::Malformed, header.error()});
        }
        m_buffer.resize(protocol::kFrameHeaderSize + header->length);
        if (!stream.readExact(std::span(m_buffer).subspan(protocol::kFrameHeaderSize))) {
            return makeUnexpected(lostFailure(stream));
        }
        return std::span<const std::uint8_t>(m_buffer);
    }

    bool send(std::span<const std::uint8_t> frames) override { return streamOf(m_stream).writeAll(frames); }

    void setSendTimeout(std::chrono::milliseconds timeout) override { streamOf(m_stream).setWriteTimeout(timeout); }

    void drain(std::chrono::steady_clock::time_point deadline) override
    {
        auto& stream = streamOf(m_stream);
        std::array<std::uint8_t, 4096> scratch{};
        while (true) {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) {
                return;
            }
            stream.setReadTimeout(left);
            if (stream.readSome(scratch) == 0) {
                return;
            }
        }
    }

    void interrupt() noexcept override { streamOf(m_stream).interrupt(); }

    [[nodiscard]] std::string closedReason() override { return closedFailure(streamOf(m_stream)).detail; }

private:
    Stream m_stream;
    protocol::FrameContext m_context;
    std::vector<std::uint8_t> m_buffer;
};

template <typename Stream>
class StreamMediaChannel final : public MediaChannel
{
public:
    StreamMediaChannel(Stream stream, protocol::MediaChannel channel) noexcept
        : m_stream(std::move(stream)), m_channel(channel)
    {
    }

    Expected<Packet, ChannelFailure> receive() override
    {
        auto& stream = streamOf(m_stream);
        m_buffer.resize(protocol::kMediaHeaderSize);
        if (!stream.readExact(m_buffer)) {
            return makeUnexpected(closedFailure(stream));
        }
        const auto header = protocol::decodeMediaHeader(m_channel, m_buffer);
        if (!header) {
            return makeUnexpected(ChannelFailure{ChannelError::Malformed, header.error()});
        }
        m_buffer.resize(protocol::kMediaHeaderSize + header->size);
        if (!stream.readExact(std::span(m_buffer).subspan(protocol::kMediaHeaderSize))) {
            return makeUnexpected(closedFailure(stream));
        }
        return Packet{std::span<const std::uint8_t>(m_buffer), 0};
    }

    void interrupt() noexcept override { streamOf(m_stream).interrupt(); }

private:
    Stream m_stream;
    protocol::MediaChannel m_channel;
    std::vector<std::uint8_t> m_buffer;
};

}
