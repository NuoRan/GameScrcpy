#include "StreamTransport.h"

#include "Handshake.h"
#include "StreamChannels.h"

#include <utility>

namespace gs::session::detail {

namespace {

using StreamHandle = std::unique_ptr<ByteStream>;

Expected<StreamHandle, SessionFailure> openFor(PhoneStreams& streams, StreamChannel channel, const LaunchParams& params,
                                               std::chrono::milliseconds timeout, const std::stop_token& stop)
{
    auto stream = streams.open(channel, params, timeout, stop);
    if (stream) {
        (*stream)->setReadTimeout(timeout);
        (*stream)->setWriteTimeout(timeout);
    }
    return stream;
}

class StreamLink final : public Link
{
public:
    StreamLink(std::shared_ptr<PhoneStreams> streams, LaunchParams params, std::chrono::milliseconds handshakeTimeout,
               StreamHandle control, protocol::Welcome welcome)
        : m_streams(std::move(streams)), m_params(std::move(params)), m_handshakeTimeout(handshakeTimeout),
          m_control(std::move(control), protocol::FrameContext::Control), m_welcome(std::move(welcome))
    {
    }

    TransportKind kind() const noexcept override { return TransportKind::Extension; }

    const protocol::Welcome& welcome() const override { return m_welcome; }

    FrameChannel& control() override { return m_control; }

    Expected<std::unique_ptr<MediaChannel>, SessionFailure> openMedia(protocol::MediaChannel channel,
                                                                      std::stop_token stop) override
    {
        const bool video = channel == protocol::MediaChannel::Video;
        auto stream = channelStream(video ? StreamChannel::Video : StreamChannel::Audio,
                                    video ? protocol::ChannelId::Video : protocol::ChannelId::Audio, stop);
        if (!stream) {
            return makeUnexpected(std::move(stream.error()));
        }
        return std::unique_ptr<MediaChannel>(
            std::make_unique<StreamMediaChannel<StreamHandle>>(std::move(*stream), channel));
    }

    Expected<std::unique_ptr<FrameChannel>, SessionFailure> openBulk(std::stop_token stop) override
    {
        auto stream = channelStream(StreamChannel::Bulk, protocol::ChannelId::Bulk, stop);
        if (!stream) {
            return makeUnexpected(std::move(stream.error()));
        }
        return std::unique_ptr<FrameChannel>(
            std::make_unique<StreamFrameChannel<StreamHandle>>(std::move(*stream), protocol::FrameContext::Bulk));
    }

private:
    Expected<StreamHandle, SessionFailure> channelStream(StreamChannel channel, protocol::ChannelId id,
                                                         const std::stop_token& stop)
    {
        auto stream = openFor(*m_streams, channel, m_params, m_handshakeTimeout, stop);
        if (!stream) {
            return stream;
        }
        {
            const std::stop_callback interrupt(stop, [&opened = **stream] { opened.interrupt(); });
            if (auto accepted = openChannel(**stream, m_params.token, id); !accepted) {
                return makeUnexpected(std::move(accepted.error()));
            }
        }
        (*stream)->setReadTimeout(std::chrono::milliseconds::zero());
        return stream;
    }

    std::shared_ptr<PhoneStreams> m_streams;
    LaunchParams m_params;
    std::chrono::milliseconds m_handshakeTimeout;
    StreamFrameChannel<StreamHandle> m_control;
    protocol::Welcome m_welcome;
};

}

StreamTransport::StreamTransport(std::shared_ptr<PhoneStreams> streams, LaunchParams params,
                                 std::chrono::milliseconds handshakeTimeout) noexcept
    : m_streams(std::move(streams)), m_params(std::move(params)), m_handshakeTimeout(handshakeTimeout)
{
}

Expected<std::unique_ptr<Link>, SessionFailure> StreamTransport::establish(const Token& token,
                                                                           const std::string& clientVersion,
                                                                           std::stop_token stop)
{
    LaunchParams params = m_params;
    params.token = token;
    auto stream = openFor(*m_streams, StreamChannel::Control, params, m_handshakeTimeout, stop);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    Expected<protocol::Welcome, SessionFailure> welcome = [&] {
        const std::stop_callback interrupt(stop, [&opened = **stream] { opened.interrupt(); });
        return handshake(**stream, token, clientVersion);
    }();
    if (!welcome) {
        return makeUnexpected(std::move(welcome.error()));
    }
    (*stream)->setReadTimeout(std::chrono::milliseconds::zero());
    return std::unique_ptr<Link>(std::make_unique<StreamLink>(m_streams, std::move(params), m_handshakeTimeout,
                                                              std::move(*stream), std::move(*welcome)));
}

}
