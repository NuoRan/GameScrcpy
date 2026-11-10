#include "DirectHandshake.h"

#include "Handshake.h"

#include "gs/protocol/Codec.h"
#include "gs/protocol/Datagram.h"

#include <algorithm>
#include <format>
#include <utility>
#include <variant>

namespace gs::session::detail {

namespace {

std::vector<std::uint8_t> handshakeDatagram(std::span<const std::uint8_t> frame = {})
{
    std::vector<std::uint8_t> datagram(protocol::kDatagramHeaderSize + frame.size());
    protocol::encodeDatagramHeader({.channel = protocol::kDatagramHandshakeChannel, .connection = 0},
                                   std::span(datagram).first<protocol::kDatagramHeaderSize>());
    std::ranges::copy(frame, datagram.begin() + protocol::kDatagramHeaderSize);
    return datagram;
}

Expected<protocol::ToClientMessage, protocol::DecodeError> handshakeMessage(std::span<const std::uint8_t> payload)
{
    const auto header = protocol::decodeFrameHeader(protocol::FrameContext::Handshake, protocol::Direction::ToClient, payload);
    if (!header) {
        return makeUnexpected(header.error());
    }
    if (payload.size() > protocol::kFrameHeaderSize + header->length) {
        return makeUnexpected(protocol::DecodeError::TrailingBytes);
    }
    return protocol::decodeToClientFrame(protocol::FrameContext::Handshake, payload);
}

}

DirectHandshake::DirectHandshake(const Token& token, std::string clientVersion, const platform::UdpEndpoint& server,
                                 TimePoint now)
    : m_token(token), m_clientVersion(std::move(clientVersion)), m_server(server), m_startedAt(now),
      m_datagram(handshakeDatagram())
{
}

void DirectHandshake::receive(std::span<const std::uint8_t> datagram, const platform::UdpEndpoint& from)
{
    if (finished()) {
        return;
    }
    if (from != m_server) {
        return ignore(std::format("a datagram from {}", from.toString()));
    }
    const auto header = protocol::decodeDatagramHeader(datagram);
    if (!header) {
        return ignore(std::format("a malformed datagram ({})", protocol::errorName(header.error())));
    }
    if (header->channel != protocol::kDatagramHandshakeChannel) {
        return ignore(std::format("a datagram on channel {}", header->channel));
    }
    const auto payload = datagram.subspan(protocol::kDatagramHeaderSize);
    if (payload.empty()) {
        if (m_state != State::Proving) {
            return ignore("an acceptance before the ClientHello");
        }
        if (header->connection == 0) {
            return ignore("an acceptance without a connection number");
        }
        m_connection = header->connection;
        m_state = State::Accepted;
        return;
    }
    const auto message = handshakeMessage(payload);
    if (!message) {
        return ignore(std::format("a malformed handshake message ({})", protocol::errorName(message.error())));
    }

    if (const auto* hello = std::get_if<protocol::ServerHello>(&*message)) {
        if (hello->protocolVersion != protocol::kProtocolVersion) {
            return fail(FailureKind::HandshakeRejected,
                        std::format("the server speaks protocol {}, this client protocol {}", hello->protocolVersion,
                                    protocol::kProtocolVersion),
                        protocol::ErrorCode::VersionMismatch);
        }
        if (m_state == State::Knocking || hello->nonce != m_nonce) {
            prove(hello->nonce);
        }
        return;
    }
    if (const auto* error = std::get_if<protocol::HandshakeError>(&*message)) {
        return fail(FailureKind::HandshakeRejected,
                    std::format("the server refused the handshake ({}): {}", protocol::enumName(error->code),
                                error->message),
                    error->code);
    }
    ignore(std::format("an unexpected {}",
                       std::visit([](const auto& m) { return std::decay_t<decltype(m)>::kName; }, *message)));
}

std::span<const std::uint8_t> DirectHandshake::poll(TimePoint now)
{
    if (finished()) {
        return {};
    }
    if (now - m_startedAt >= kGiveUpAfter) {
        std::string detail = std::format("no {} from {} within {} ms",
                                         m_state == State::Knocking ? "ServerHello" : "acceptance", m_server.toString(),
                                         kGiveUpAfter.count());
        if (m_ignored != 0) {
            detail += std::format("; ignored {} datagrams, the last {}", m_ignored, m_lastIgnored);
        }
        fail(FailureKind::DirectUnreachable, std::move(detail));
        return {};
    }
    if (m_sentAt && now - *m_sentAt < kRetransmitInterval) {
        return {};
    }
    m_sentAt = now;
    return m_datagram;
}

DirectHandshake::TimePoint DirectHandshake::deadline() const noexcept
{
    if (finished()) {
        return TimePoint::max();
    }
    if (!m_sentAt) {
        return m_startedAt;
    }
    return std::min(*m_sentAt + kRetransmitInterval, m_startedAt + kGiveUpAfter);
}

void DirectHandshake::prove(const protocol::Nonce& nonce)
{
    protocol::ClientHello hello;
    hello.clientVersion = m_clientVersion;
    hello.proof = udpHelloProof(m_token, nonce);
    const auto frame = protocol::encodeFrame(hello);
    if (!frame) {
        return fail(FailureKind::ProtocolError,
                    std::format("cannot encode the ClientHello: {}", protocol::errorName(frame.error())));
    }
    m_nonce = nonce;
    m_datagram = handshakeDatagram(*frame);
    m_sentAt.reset();
    m_state = State::Proving;
}

void DirectHandshake::fail(FailureKind kind, std::string detail, protocol::ErrorCode code)
{
    m_failure.kind = kind;
    m_failure.code = code;
    m_failure.detail = std::move(detail);
    m_state = State::Failed;
}

void DirectHandshake::ignore(std::string why)
{
    ++m_ignored;
    m_lastIgnored = std::move(why);
}

}
