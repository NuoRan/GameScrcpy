#include "DirectTransport.h"

#include "DirectHandshake.h"
#include "ReliableStream.h"

#include "gs/platform/Thread.h"
#include "gs/protocol/Codec.h"
#include "gs/protocol/Datagram.h"
#include "gs/protocol/DirectMedia.h"
#include "gs/protocol/Reliable.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace gs::session::detail {

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr std::size_t kControlReceiveLimit = std::size_t{1} << 20;
constexpr std::size_t kBulkReceiveLimit = std::size_t{protocol::kBulkPayloadLimit} + protocol::kFrameHeaderSize;
constexpr std::size_t kReceiveBufferBytes = std::size_t{4} << 20;
constexpr std::chrono::milliseconds kOpenResendInterval{250};
constexpr std::chrono::milliseconds kOpenGiveUpAfter{1500};

struct MediaState {
    bool requested = false;
    bool opened = false;
    bool interrupted = false;
    bool closed = false;
    protocol::VideoAssembler video;
    protocol::AudioAssembler audio;
    std::uint64_t lostTaken = 0;
    std::uint64_t rebuiltTaken = 0;
};

SessionFailure failure(FailureKind kind, std::string detail, protocol::ErrorCode code = protocol::ErrorCode::None)
{
    SessionFailure result;
    result.kind = kind;
    result.code = code;
    result.detail = std::move(detail);
    return result;
}

class DirectLink;

class ControlChannel final : public FrameChannel
{
public:
    explicit ControlChannel(DirectLink& link) noexcept : m_link(link) {}

    Expected<std::span<const std::uint8_t>, ChannelFailure> receive() override;
    bool send(std::span<const std::uint8_t> frames) override;
    void setSendTimeout(std::chrono::milliseconds) override {}
    void drain(SteadyClock::time_point deadline) override;
    void interrupt() noexcept override;

private:
    DirectLink& m_link;
    std::vector<std::uint8_t> m_frame;
};

class BulkChannel final : public FrameChannel
{
public:
    explicit BulkChannel(DirectLink& link) noexcept : m_link(link) {}

    Expected<std::span<const std::uint8_t>, ChannelFailure> receive() override;
    bool send(std::span<const std::uint8_t>) override { return false; }
    void setSendTimeout(std::chrono::milliseconds) override {}
    void drain(SteadyClock::time_point) override {}
    void interrupt() noexcept override;

private:
    DirectLink& m_link;
    std::vector<std::uint8_t> m_frame;
};

class DirectMediaChannel final : public MediaChannel
{
public:
    DirectMediaChannel(DirectLink& link, std::uint8_t channel) noexcept : m_link(link), m_channel(channel) {}

    Expected<Packet, ChannelFailure> receive() override;
    void interrupt() noexcept override;
    LossCounts takeLossCounts() noexcept override;

private:
    DirectLink& m_link;
    std::uint8_t m_channel;
    std::vector<std::uint8_t> m_packet;
};

class DirectLink final : public Link
{
public:
    DirectLink(platform::UdpSocket socket, const platform::UdpEndpoint& server, std::uint32_t connection,
               const Token& token)
        : m_socket(std::move(socket)), m_server(server), m_connection(connection),
          m_controlStream(ReliableStream::Kind::Control, connection, token, kControlReceiveLimit),
          m_bulkStream(ReliableStream::Kind::Bulk, connection, token, kBulkReceiveLimit), m_control(*this)
    {
        m_receiver = std::jthread([this] { receiveLoop(); });
        m_sender = std::jthread([this] { sendLoop(); });
    }

    ~DirectLink() override
    {
        close();
        m_receiver = {};
        m_sender = {};
    }

    DirectLink(const DirectLink&) = delete;
    DirectLink& operator=(const DirectLink&) = delete;

    TransportKind kind() const noexcept override { return TransportKind::WifiDirect; }

    const protocol::Welcome& welcome() const override { return m_welcome; }

    FrameChannel& control() override { return m_control; }

    Expected<std::unique_ptr<MediaChannel>, SessionFailure> openMedia(protocol::MediaChannel channel,
                                                                      std::stop_token stop) override
    {
        const bool video = channel == protocol::MediaChannel::Video;
        const std::uint8_t number = video ? protocol::kVideoChannel : protocol::kAudioChannel;
        const auto open = protocol::encodeDatagramHeader({.channel = number, .connection = m_connection});
        const auto deadline = SteadyClock::now() + kOpenGiveUpAfter;
        const std::stop_callback wake(stop, [this] { m_changed.notify_all(); });
        std::unique_lock lock(m_mutex);
        m_media[number] = MediaState{};
        m_media[number].requested = true;
        while (!m_media[number].opened) {
            if (m_closed || stop.stop_requested()) {
                return makeUnexpected(failure(FailureKind::ConnectFailed, "opening a media channel was cut short"));
            }
            const auto now = SteadyClock::now();
            if (now >= deadline) {
                m_media[number].requested = false;
                return makeUnexpected(failure(FailureKind::HandshakeRejected,
                                              std::format("the server did not open the {} channel", video ? "video" : "audio"),
                                              protocol::ErrorCode::ChannelUnavailable));
            }
            static_cast<void>(m_socket.sendTo(m_server, open));
            m_changed.wait_until(lock, std::min(now + kOpenResendInterval, deadline));
        }
        return std::unique_ptr<MediaChannel>(std::make_unique<DirectMediaChannel>(*this, number));
    }

    Expected<std::unique_ptr<FrameChannel>, SessionFailure> openBulk(std::stop_token) override
    {
        {
            const std::lock_guard lock(m_mutex);
            m_bulkInterrupted = false;
        }
        return std::unique_ptr<FrameChannel>(std::make_unique<BulkChannel>(*this));
    }

    Expected<void, SessionFailure> awaitWelcome(SteadyClock::time_point deadline, const std::stop_token& stop)
    {
        const std::stop_callback interrupt(stop, [this] { close(); });
        std::unique_lock lock(m_mutex);
        while (true) {
            if (m_closed) {
                return makeUnexpected(failure(FailureKind::ConnectFailed, "the handshake was cut short"));
            }
            if (m_broken) {
                return makeUnexpected(failure(FailureKind::ProtocolError, "the server broke the reliable channel"));
            }
            const auto frame = takeFrame(m_controlStream, protocol::FrameContext::Handshake, m_welcomeFrame);
            if (!frame) {
                return makeUnexpected(failure(FailureKind::ProtocolError,
                                              std::format("malformed first control frame: {}",
                                                          protocol::errorName(frame.error()))));
            }
            if (*frame) {
                break;
            }
            if (m_changed.wait_until(lock, deadline) == std::cv_status::timeout && SteadyClock::now() >= deadline) {
                return makeUnexpected(failure(FailureKind::Timeout, "no Welcome on the control stream in time"));
            }
        }
        lock.unlock();
        const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Handshake, m_welcomeFrame);
        if (!message) {
            return makeUnexpected(failure(FailureKind::ProtocolError,
                                          std::format("malformed Welcome: {}", protocol::errorName(message.error()))));
        }
        auto* welcome = std::get_if<protocol::Welcome>(&*message);
        if (welcome == nullptr) {
            return makeUnexpected(failure(FailureKind::ProtocolError, "the control stream did not start with a Welcome"));
        }
        m_welcome = std::move(*welcome);
        return {};
    }

    void close() noexcept
    {
        {
            const std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_changed.notify_all();
        m_socket.interrupt();
    }

private:
    friend class ControlChannel;
    friend class BulkChannel;
    friend class DirectMediaChannel;

    static Expected<bool, protocol::DecodeError> takeFrame(ReliableStream& stream, protocol::FrameContext context,
                                                           std::vector<std::uint8_t>& out)
    {
        const auto bytes = stream.readable();
        if (bytes.size() < protocol::kFrameHeaderSize) {
            return false;
        }
        const auto header = protocol::decodeFrameHeader(context, protocol::Direction::ToClient, bytes);
        if (!header) {
            return makeUnexpected(header.error());
        }
        const std::size_t size = protocol::kFrameHeaderSize + header->length;
        if (bytes.size() < size) {
            return false;
        }
        out.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        stream.consume(size);
        return true;
    }

    void receiveLoop()
    {
        platform::setCurrentThreadName("gs-udp-in");
        platform::setCurrentThreadRole(platform::ThreadRole::Media);
        std::array<std::uint8_t, protocol::kDatagramMaxSize + 1> buffer{};
        while (const auto received = m_socket.receive(buffer)) {
            if (received->from != m_server) {
                continue;
            }
            const auto datagram = std::span<const std::uint8_t>(buffer).first(received->size);
            const auto header = protocol::decodeDatagramHeader(datagram);
            if (!header || header->connection != m_connection) {
                continue;
            }
            {
                const std::lock_guard lock(m_mutex);
                if (header->channel == protocol::kVideoChannel || header->channel == protocol::kAudioChannel) {
                    if (protocol::isMediaClosed(datagram)) {
                        static_cast<void>(
                            m_socket.sendTo(m_server, protocol::encodeMediaClosed(header->channel, m_connection)));
                        m_media[header->channel].closed = true;
                    } else {
                        take(m_media[header->channel], header->channel, datagram);
                    }
                } else {
                    ReliableStream& stream =
                        header->channel == protocol::kControlChannel ? m_controlStream : m_bulkStream;
                    if (stream.receive(datagram, SteadyClock::now()).verdict == ReliableStream::Verdict::Broken) {
                        m_broken = true;
                    }
                }
            }
            m_changed.notify_all();
        }
    }

    void sendLoop()
    {
        platform::setCurrentThreadName("gs-udp-out");
        platform::setCurrentThreadRole(platform::ThreadRole::Input);
        std::unique_lock lock(m_mutex);
        while (!m_closed) {
            const auto now = SteadyClock::now();
            for (ReliableStream* stream : {&m_controlStream, &m_bulkStream}) {
                for (auto datagram = stream->poll(now); !datagram.empty(); datagram = stream->poll(now)) {
                    static_cast<void>(m_socket.sendTo(m_server, datagram));
                }
            }
            const auto wake = std::min(m_controlStream.deadline(), m_bulkStream.deadline());
            if (wake == SteadyClock::time_point::max()) {
                m_changed.wait(lock);
            } else if (wake > now) {
                m_changed.wait_until(lock, wake);
            }
        }
    }

    static void take(MediaState& media, std::uint8_t channel, std::span<const std::uint8_t> datagram)
    {
        if (datagram.size() == protocol::kDatagramHeaderSize) {
            media.opened = media.requested;
            return;
        }
        if (!media.opened) {
            return;
        }
        if (channel == protocol::kVideoChannel) {
            if (const auto shard = protocol::decodeVideoShard(datagram)) {
                media.video.add(*shard);
            }
        } else if (const auto audio = protocol::decodeAudioDatagram(datagram)) {
            media.audio.add(*audio);
        }
    }

    platform::UdpSocket m_socket;
    platform::UdpEndpoint m_server;
    std::uint32_t m_connection;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    ReliableStream m_controlStream;
    ReliableStream m_bulkStream;
    std::array<MediaState, 3> m_media;
    bool m_closed = false;
    bool m_broken = false;
    bool m_goodbyeSent = false;
    bool m_bulkInterrupted = false;
    ControlChannel m_control;
    protocol::Welcome m_welcome;
    std::vector<std::uint8_t> m_welcomeFrame;
    std::jthread m_receiver;
    std::jthread m_sender;
};

Expected<std::span<const std::uint8_t>, ChannelFailure> ControlChannel::receive()
{
    std::unique_lock lock(m_link.m_mutex);
    while (true) {
        if (m_link.m_broken) {
            return makeUnexpected(ChannelFailure{ChannelError::Malformed, protocol::DecodeError::OutOfRange});
        }
        const auto frame = DirectLink::takeFrame(m_link.m_controlStream, protocol::FrameContext::Control, m_frame);
        if (!frame) {
            return makeUnexpected(ChannelFailure{ChannelError::Malformed, frame.error()});
        }
        if (*frame) {
            return std::span<const std::uint8_t>(m_frame);
        }
        if (m_link.m_closed || (m_link.m_goodbyeSent && m_link.m_controlStream.settled())) {
            return makeUnexpected(ChannelFailure{ChannelError::Closed});
        }
        m_link.m_changed.wait(lock);
    }
}

bool ControlChannel::send(std::span<const std::uint8_t> frames)
{
    {
        const std::lock_guard lock(m_link.m_mutex);
        if (m_link.m_closed || m_link.m_broken) {
            return false;
        }
        for (auto rest = frames; rest.size() >= protocol::kFrameHeaderSize;) {
            const auto type = static_cast<std::uint16_t>((rest[0] << 8) | rest[1]);
            const auto length = (std::uint32_t{rest[2]} << 24) | (std::uint32_t{rest[3]} << 16) |
                                (std::uint32_t{rest[4]} << 8) | std::uint32_t{rest[5]};
            m_link.m_goodbyeSent = m_link.m_goodbyeSent || type == static_cast<std::uint16_t>(protocol::Goodbye::kType);
            rest = rest.subspan(std::min<std::size_t>(rest.size(), protocol::kFrameHeaderSize + length));
        }
        m_link.m_controlStream.send(frames);
    }
    m_link.m_changed.notify_all();
    return true;
}

void ControlChannel::drain(SteadyClock::time_point deadline)
{
    std::unique_lock lock(m_link.m_mutex);
    m_link.m_changed.wait_until(lock, deadline,
                                [this] { return m_link.m_closed || m_link.m_controlStream.settled(); });
}

void ControlChannel::interrupt() noexcept
{
    m_link.close();
}

Expected<std::span<const std::uint8_t>, ChannelFailure> BulkChannel::receive()
{
    std::unique_lock lock(m_link.m_mutex);
    while (true) {
        const auto frame = DirectLink::takeFrame(m_link.m_bulkStream, protocol::FrameContext::Bulk, m_frame);
        if (!frame) {
            return makeUnexpected(ChannelFailure{ChannelError::Malformed, frame.error()});
        }
        if (*frame) {
            return std::span<const std::uint8_t>(m_frame);
        }
        if (m_link.m_closed || m_link.m_bulkInterrupted) {
            return makeUnexpected(ChannelFailure{ChannelError::Closed});
        }
        m_link.m_changed.wait(lock);
    }
}

void BulkChannel::interrupt() noexcept
{
    {
        const std::lock_guard lock(m_link.m_mutex);
        m_link.m_bulkInterrupted = true;
    }
    m_link.m_changed.notify_all();
}

Expected<MediaChannel::Packet, ChannelFailure> DirectMediaChannel::receive()
{
    std::unique_lock lock(m_link.m_mutex);
    MediaState& media = m_link.m_media[m_channel];
    while (true) {
        if (media.interrupted || m_link.m_closed) {
            return makeUnexpected(ChannelFailure{ChannelError::Closed});
        }
        auto delivery = m_channel == protocol::kVideoChannel ? media.video.next() : media.audio.next();
        if (delivery) {
            m_packet = std::move(delivery->bytes);
            return Packet{std::span<const std::uint8_t>(m_packet), delivery->lostBefore};
        }
        if (media.closed) {
            return makeUnexpected(ChannelFailure{ChannelError::Closed});
        }
        m_link.m_changed.wait(lock);
    }
}

void DirectMediaChannel::interrupt() noexcept
{
    {
        const std::lock_guard lock(m_link.m_mutex);
        m_link.m_media[m_channel].interrupted = true;
    }
    m_link.m_changed.notify_all();
}

MediaChannel::LossCounts DirectMediaChannel::takeLossCounts() noexcept
{
    const std::lock_guard lock(m_link.m_mutex);
    MediaState& media = m_link.m_media[m_channel];
    const auto& stats = media.video.stats();
    const LossCounts counts{static_cast<std::uint32_t>(stats.shardsLost - media.lostTaken),
                            static_cast<std::uint32_t>(stats.shardsRecovered - media.rebuiltTaken)};
    media.lostTaken = stats.shardsLost;
    media.rebuiltTaken = stats.shardsRecovered;
    return counts;
}

}

Expected<std::unique_ptr<Link>, SessionFailure> DirectTransport::establish(const Token& token,
                                                                           const std::string& clientVersion,
                                                                           std::stop_token stop)
{
    auto socket = platform::UdpSocket::bind(platform::UdpEndpoint::any(m_server.v6, 0));
    if (!socket) {
        return makeUnexpected(failure(FailureKind::ConnectFailed, "cannot open a UDP socket"));
    }
    static_cast<void>(socket->setReceiveBufferSize(kReceiveBufferBytes));
    const auto started = SteadyClock::now();
    DirectHandshake handshake(token, clientVersion, m_server, started);
    {
        const std::stop_callback interrupt(stop, [&socket] { socket->interrupt(); });
        std::array<std::uint8_t, protocol::kDatagramMaxSize + 1> buffer{};
        while (true) {
            const auto now = SteadyClock::now();
            if (const auto datagram = handshake.poll(now); !datagram.empty()) {
                static_cast<void>(socket->sendTo(m_server, datagram));
            }
            using State = DirectHandshake::State;
            if (handshake.state() == State::Accepted || handshake.state() == State::Failed) {
                break;
            }
            const auto wait = std::chrono::ceil<std::chrono::milliseconds>(handshake.deadline() - now);
            socket->setReceiveTimeout(std::max(wait, std::chrono::milliseconds{1}));
            if (const auto received = socket->receive(buffer)) {
                handshake.receive(std::span<const std::uint8_t>(buffer).first(received->size), received->from);
            } else if (!socket->receiveTimedOut()) {
                return makeUnexpected(failure(FailureKind::ConnectFailed, "the handshake was cut short"));
            }
        }
    }
    if (handshake.state() == DirectHandshake::State::Failed) {
        return makeUnexpected(handshake.failure());
    }
    socket->setReceiveTimeout(std::chrono::milliseconds::zero());
    auto link = std::make_unique<DirectLink>(std::move(*socket), m_server, handshake.connection(), token);
    if (auto welcomed = link->awaitWelcome(started + DirectHandshake::kGiveUpAfter, stop); !welcomed) {
        return makeUnexpected(std::move(welcomed.error()));
    }
    return std::unique_ptr<Link>(std::move(link));
}

}
