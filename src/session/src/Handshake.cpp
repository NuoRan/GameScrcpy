#include "Handshake.h"

#include "StreamChannels.h"

#include "gs/platform/Crypto.h"

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <utility>
#include <variant>

namespace gs::session::detail {

namespace {

constexpr std::string_view kHelloContext = "GSCP/1 hello";
constexpr std::string_view kChannelContext = "GSCP/1 channel";

std::span<const std::uint8_t> bytesOf(std::string_view text) noexcept
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

SessionFailure failure(FailureKind kind, std::string detail, protocol::ErrorCode code = protocol::ErrorCode::None)
{
    SessionFailure result;
    result.kind = kind;
    result.code = code;
    result.detail = std::move(detail);
    return result;
}

SessionFailure rejected(const protocol::HandshakeError& error)
{
    return failure(FailureKind::HandshakeRejected,
                   std::format("the server refused the handshake ({}): {}", protocol::enumName(error.code), error.message),
                   error.code);
}

std::string_view nameOf(const protocol::ToClientMessage& message)
{
    return std::visit([](const auto& m) { return std::decay_t<decltype(m)>::kName; }, message);
}

SessionFailure unexpected(const protocol::ToClientMessage& message, std::string_view expected)
{
    return failure(FailureKind::ProtocolError, std::format("expected {}, got {}", expected, nameOf(message)));
}

template <typename Stream, typename M>
Expected<void, SessionFailure> write(Stream& stream, const M& message)
{
    const auto frame = protocol::encodeFrame(message);
    if (!frame) {
        return makeUnexpected(failure(FailureKind::ProtocolError,
                                      std::format("cannot encode {}: {}", M::kName, protocol::errorName(frame.error()))));
    }
    if (!stream.writeAll(*frame)) {
        return makeUnexpected(failure(FailureKind::ConnectFailed, std::format("the connection broke sending {}", M::kName)));
    }
    return {};
}

template <typename Stream>
Expected<protocol::ToClientMessage, ReadFailure> readFrom(Stream& stream, protocol::FrameContext context,
                                                          std::vector<std::uint8_t>& buffer)
{
    std::array<std::uint8_t, protocol::kFrameHeaderSize> head{};
    if (!stream.readExact(head)) {
        return makeUnexpected(lostFailure(stream));
    }
    const auto header = protocol::decodeFrameHeader(context, protocol::Direction::ToClient, head);
    if (!header) {
        return makeUnexpected(ReadFailure{ReadError::Malformed, header.error()});
    }
    buffer.resize(header->length);
    if (!stream.readExact(buffer)) {
        return makeUnexpected(lostFailure(stream));
    }
    auto message = protocol::decodeToClientPayload(header->type, buffer);
    if (!message) {
        return makeUnexpected(ReadFailure{ReadError::Malformed, message.error()});
    }
    return std::move(*message);
}

template <typename Stream>
Expected<protocol::Nonce, SessionFailure> greeting(Stream& stream, std::vector<std::uint8_t>& buffer)
{
    const auto message = readFrom(stream, protocol::FrameContext::Handshake, buffer);
    if (!message) {
        return makeUnexpected(readFailure(message.error(), "the server greeting"));
    }
    if (const auto* error = std::get_if<protocol::HandshakeError>(&*message)) {
        return makeUnexpected(rejected(*error));
    }
    const auto* hello = std::get_if<protocol::ServerHello>(&*message);
    if (hello == nullptr) {
        return makeUnexpected(unexpected(*message, protocol::ServerHello::kName));
    }
    if (hello->protocolVersion != protocol::kProtocolVersion) {
        return makeUnexpected(failure(FailureKind::HandshakeRejected,
                                      std::format("the server speaks protocol {}, this client protocol {}",
                                                  hello->protocolVersion, protocol::kProtocolVersion),
                                      protocol::ErrorCode::VersionMismatch));
    }
    return hello->nonce;
}

template <typename Stream>
Expected<protocol::Welcome, SessionFailure> handshakeOn(Stream& stream, const Token& token,
                                                        const std::string& clientVersion)
{
    std::vector<std::uint8_t> buffer;
    const auto nonce = greeting(stream, buffer);
    if (!nonce) {
        return makeUnexpected(nonce.error());
    }
    protocol::ClientHello hello;
    hello.clientVersion = clientVersion;
    hello.proof = helloProof(token, *nonce);
    if (auto sent = write(stream, hello); !sent) {
        return makeUnexpected(std::move(sent.error()));
    }
    auto reply = readFrom(stream, protocol::FrameContext::Handshake, buffer);
    if (!reply) {
        return makeUnexpected(readFailure(reply.error(), "the Welcome"));
    }
    if (auto* welcome = std::get_if<protocol::Welcome>(&*reply)) {
        return std::move(*welcome);
    }
    if (const auto* error = std::get_if<protocol::HandshakeError>(&*reply)) {
        return makeUnexpected(rejected(*error));
    }
    return makeUnexpected(unexpected(*reply, protocol::Welcome::kName));
}

template <typename Stream>
Expected<void, SessionFailure> openOn(Stream& stream, const Token& token, protocol::ChannelId channel)
{
    std::vector<std::uint8_t> buffer;
    const auto nonce = greeting(stream, buffer);
    if (!nonce) {
        return makeUnexpected(nonce.error());
    }
    protocol::ChannelOpen open;
    open.channel = channel;
    open.proof = channelProof(token, *nonce, channel);
    if (auto sent = write(stream, open); !sent) {
        return makeUnexpected(std::move(sent.error()));
    }
    const auto reply = readFrom(stream, protocol::FrameContext::Handshake, buffer);
    if (!reply) {
        return makeUnexpected(readFailure(reply.error(), "the channel acceptance"));
    }
    if (const auto* accepted = std::get_if<protocol::ChannelAccepted>(&*reply)) {
        if (accepted->channel == channel) {
            return {};
        }
        return makeUnexpected(failure(FailureKind::ProtocolError,
                                      std::format("asked for channel {}, the server accepted {}",
                                                  protocol::enumName(channel), protocol::enumName(accepted->channel))));
    }
    if (const auto* error = std::get_if<protocol::HandshakeError>(&*reply)) {
        return makeUnexpected(rejected(*error));
    }
    return makeUnexpected(unexpected(*reply, protocol::ChannelAccepted::kName));
}

}

protocol::Proof helloProof(const Token& token, const protocol::Nonce& nonce)
{
    return platform::hmacSha256(token, {bytesOf(kHelloContext), nonce});
}

protocol::Proof channelProof(const Token& token, const protocol::Nonce& nonce, protocol::ChannelId channel)
{
    const std::array<std::uint8_t, 1> number{static_cast<std::uint8_t>(channel)};
    return platform::hmacSha256(token, {bytesOf(kChannelContext), nonce, number});
}

protocol::Proof udpHelloProof(const Token& token, const protocol::Nonce& nonce)
{
    return platform::hmacSha256(token, {bytesOf(protocol::kUdpHelloContext), nonce});
}

std::array<std::uint8_t, protocol::kControlTagSize> controlTag(const Token& token, std::span<const std::uint8_t> preceding)
{
    const platform::Sha256Digest digest = platform::hmacSha256(token, {bytesOf(protocol::kUdpControlContext), preceding});
    std::array<std::uint8_t, protocol::kControlTagSize> tag{};
    std::copy_n(digest.begin(), tag.size(), tag.begin());
    return tag;
}

Expected<protocol::ToClientMessage, ReadFailure> readMessage(platform::TcpStream& stream, protocol::FrameContext context,
                                                             std::vector<std::uint8_t>& buffer)
{
    return readFrom(stream, context, buffer);
}

Expected<protocol::ToClientMessage, ReadFailure> readMessage(ByteStream& stream, protocol::FrameContext context,
                                                             std::vector<std::uint8_t>& buffer)
{
    return readFrom(stream, context, buffer);
}

SessionFailure readFailure(const ReadFailure& failed, std::string_view what)
{
    switch (failed.error) {
    case ReadError::Closed: {
        std::string text = std::format("the connection closed before {}", what);
        if (!failed.detail.empty()) {
            text += ": " + failed.detail;
        }
        return failure(FailureKind::ConnectFailed, std::move(text));
    }
    case ReadError::TimedOut:
        return failure(FailureKind::Timeout, std::format("no answer waiting for {}", what));
    case ReadError::Malformed:
        break;
    }
    return failure(FailureKind::ProtocolError, std::format("malformed {}: {}", what, protocol::errorName(failed.decode)));
}

Expected<protocol::Welcome, SessionFailure> handshake(platform::TcpStream& stream, const Token& token,
                                                      const std::string& clientVersion)
{
    return handshakeOn(stream, token, clientVersion);
}

Expected<protocol::Welcome, SessionFailure> handshake(ByteStream& stream, const Token& token,
                                                      const std::string& clientVersion)
{
    return handshakeOn(stream, token, clientVersion);
}

Expected<void, SessionFailure> openChannel(platform::TcpStream& stream, const Token& token, protocol::ChannelId channel)
{
    return openOn(stream, token, channel);
}

Expected<void, SessionFailure> openChannel(ByteStream& stream, const Token& token, protocol::ChannelId channel)
{
    return openOn(stream, token, channel);
}

}
