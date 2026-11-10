#include "TunnelTransport.h"

#include "Handshake.h"

#include "gs/platform/TcpStream.h"

#include <array>
#include <utility>
#include <vector>

namespace gs::session {

std::unique_ptr<Transport> ServerHost::transport(std::chrono::milliseconds handshakeTimeout)
{
    return std::make_unique<detail::TunnelTransport>(*this, handshakeTimeout);
}

namespace detail {

namespace {

ChannelFailure ioFailure(const platform::TcpStream& stream) noexcept
{
    return {stream.readTimedOut() ? ChannelError::TimedOut : ChannelError::Closed};
}

Expected<platform::TcpStream, SessionFailure> connectFor(ServerHost& host, std::chrono::milliseconds timeout)
{
    auto stream = host.connect();
    if (stream) {
        stream->setReadTimeout(timeout);
        stream->setWriteTimeout(timeout);
    }
    return stream;
}

class StreamFrameChannel final : public FrameChannel
{
public:
    StreamFrameChannel(platform::TcpStream stream, protocol::FrameContext context) noexcept
        : m_stream(std::move(stream)), m_context(context)
    {
    }

    Expected<std::span<const std::uint8_t>, ChannelFailure> receive() override
    {
        m_buffer.resize(protocol::kFrameHeaderSize);
        if (!m_stream.readExact(m_buffer)) {
            return makeUnexpected(ioFailure(m_stream));
        }
        const auto header = protocol::decodeFrameHeader(m_context, protocol::Direction::ToClient, m_buffer);
        if (!header) {
            return makeUnexpected(ChannelFailure{ChannelError::Malformed, header.error()});
        }
        m_buffer.resize(protocol::kFrameHeaderSize + header->length);
        if (!m_stream.readExact(std::span(m_buffer).subspan(protocol::kFrameHeaderSize))) {
            return makeUnexpected(ioFailure(m_stream));
        }
        return std::span<const std::uint8_t>(m_buffer);
    }

    bool send(std::span<const std::uint8_t> frames) override { return m_stream.writeAll(frames); }

    void setSendTimeout(std::chrono::milliseconds timeout) override { m_stream.setWriteTimeout(timeout); }

    void drain(std::chrono::steady_clock::time_point deadline) override
    {
        std::array<std::uint8_t, 4096> scratch{};
        while (true) {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) {
                return;
            }
            m_stream.setReadTimeout(left);
            if (m_stream.readSome(scratch) == 0) {
                return;
            }
        }
    }

    void interrupt() noexcept override { m_stream.interrupt(); }

private:
    platform::TcpStream m_stream;
    protocol::FrameContext m_context;
    std::vector<std::uint8_t> m_buffer;
};

class StreamMediaChannel final : public MediaChannel
{
public:
    StreamMediaChannel(platform::TcpStream stream, protocol::MediaChannel channel) noexcept
        : m_stream(std::move(stream)), m_channel(channel)
    {
    }

    Expected<Packet, ChannelFailure> receive() override
    {
        m_buffer.resize(protocol::kMediaHeaderSize);
        if (!m_stream.readExact(m_buffer)) {
            return makeUnexpected(ChannelFailure{ChannelError::Closed});
        }
        const auto header = protocol::decodeMediaHeader(m_channel, m_buffer);
        if (!header) {
            return makeUnexpected(ChannelFailure{ChannelError::Malformed, header.error()});
        }
        m_buffer.resize(protocol::kMediaHeaderSize + header->size);
        if (!m_stream.readExact(std::span(m_buffer).subspan(protocol::kMediaHeaderSize))) {
            return makeUnexpected(ChannelFailure{ChannelError::Closed});
        }
        return Packet{std::span<const std::uint8_t>(m_buffer), 0};
    }

    void interrupt() noexcept override { m_stream.interrupt(); }

private:
    platform::TcpStream m_stream;
    protocol::MediaChannel m_channel;
    std::vector<std::uint8_t> m_buffer;
};

class TunnelLink final : public Link
{
public:
    TunnelLink(ServerHost& host, const Token& token, std::chrono::milliseconds handshakeTimeout,
               platform::TcpStream control, protocol::Welcome welcome)
        : m_host(host), m_token(token), m_handshakeTimeout(handshakeTimeout),
          m_control(std::move(control), protocol::FrameContext::Control), m_welcome(std::move(welcome))
    {
    }

    TransportKind kind() const noexcept override { return TransportKind::AdbTunnel; }

    const protocol::Welcome& welcome() const override { return m_welcome; }

    FrameChannel& control() override { return m_control; }

    Expected<std::unique_ptr<MediaChannel>, SessionFailure> openMedia(protocol::MediaChannel channel,
                                                                      std::stop_token stop) override
    {
        auto stream = connectFor(m_host, m_handshakeTimeout);
        if (!stream) {
            return makeUnexpected(std::move(stream.error()));
        }
        {
            const std::stop_callback interrupt(stop, [&opened = *stream] { opened.interrupt(); });
            const protocol::ChannelId id =
                channel == protocol::MediaChannel::Video ? protocol::ChannelId::Video : protocol::ChannelId::Audio;
            if (auto accepted = openChannel(*stream, m_token, id); !accepted) {
                return makeUnexpected(std::move(accepted.error()));
            }
        }
        stream->setReadTimeout(std::chrono::milliseconds::zero());
        return std::unique_ptr<MediaChannel>(std::make_unique<StreamMediaChannel>(std::move(*stream), channel));
    }

    Expected<std::unique_ptr<FrameChannel>, SessionFailure> openBulk(std::stop_token stop) override
    {
        auto stream = connectFor(m_host, m_handshakeTimeout);
        if (!stream) {
            return makeUnexpected(std::move(stream.error()));
        }
        {
            const std::stop_callback interrupt(stop, [&opened = *stream] { opened.interrupt(); });
            if (auto accepted = openChannel(*stream, m_token, protocol::ChannelId::Bulk); !accepted) {
                return makeUnexpected(std::move(accepted.error()));
            }
        }
        stream->setReadTimeout(std::chrono::milliseconds::zero());
        return std::unique_ptr<FrameChannel>(
            std::make_unique<StreamFrameChannel>(std::move(*stream), protocol::FrameContext::Bulk));
    }

private:
    ServerHost& m_host;
    Token m_token;
    std::chrono::milliseconds m_handshakeTimeout;
    StreamFrameChannel m_control;
    protocol::Welcome m_welcome;
};

}

TunnelTransport::TunnelTransport(ServerHost& host, std::chrono::milliseconds handshakeTimeout) noexcept
    : m_host(host), m_handshakeTimeout(handshakeTimeout)
{
}

Expected<std::unique_ptr<Link>, SessionFailure> TunnelTransport::establish(const Token& token,
                                                                           const std::string& clientVersion,
                                                                           std::stop_token stop)
{
    auto stream = connectFor(m_host, m_handshakeTimeout);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    Expected<protocol::Welcome, SessionFailure> welcome = [&] {
        const std::stop_callback interrupt(stop, [&opened = *stream] { opened.interrupt(); });
        return handshake(*stream, token, clientVersion);
    }();
    if (!welcome) {
        return makeUnexpected(std::move(welcome.error()));
    }
    stream->setReadTimeout(std::chrono::milliseconds::zero());
    return std::unique_ptr<Link>(
        std::make_unique<TunnelLink>(m_host, token, m_handshakeTimeout, std::move(*stream), std::move(*welcome)));
}

}
}
