#include "TunnelTransport.h"

#include "Handshake.h"
#include "StreamChannels.h"

#include "gs/platform/TcpStream.h"

#include <utility>

namespace gs::session {

std::unique_ptr<Transport> ServerHost::transport(std::chrono::milliseconds handshakeTimeout)
{
    return std::make_unique<detail::TunnelTransport>(*this, handshakeTimeout);
}

namespace detail {

namespace {

Expected<platform::TcpStream, SessionFailure> connectFor(ServerHost& host, std::chrono::milliseconds timeout)
{
    auto stream = host.connect();
    if (stream) {
        stream->setReadTimeout(timeout);
        stream->setWriteTimeout(timeout);
    }
    return stream;
}

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
        return std::unique_ptr<MediaChannel>(
            std::make_unique<StreamMediaChannel<platform::TcpStream>>(std::move(*stream), channel));
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
            std::make_unique<StreamFrameChannel<platform::TcpStream>>(std::move(*stream), protocol::FrameContext::Bulk));
    }

private:
    ServerHost& m_host;
    Token m_token;
    std::chrono::milliseconds m_handshakeTimeout;
    StreamFrameChannel<platform::TcpStream> m_control;
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
