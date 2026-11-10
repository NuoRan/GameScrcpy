#pragma once

#include "gs/base/Expected.h"
#include "gs/platform/TcpStream.h"
#include "gs/protocol/Codec.h"
#include "gs/protocol/Datagram.h"
#include "gs/session/SessionState.h"
#include "gs/session/Transport.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::session::detail {

[[nodiscard]] protocol::Proof helloProof(const Token& token, const protocol::Nonce& nonce);
[[nodiscard]] protocol::Proof channelProof(const Token& token, const protocol::Nonce& nonce, protocol::ChannelId channel);

[[nodiscard]] protocol::Proof udpHelloProof(const Token& token, const protocol::Nonce& nonce);
[[nodiscard]] std::array<std::uint8_t, protocol::kControlTagSize> controlTag(const Token& token,
                                                                            std::span<const std::uint8_t> preceding);

using ReadError = ChannelError;
using ReadFailure = ChannelFailure;

[[nodiscard]] Expected<protocol::ToClientMessage, ReadFailure> readMessage(platform::TcpStream& stream,
                                                                         protocol::FrameContext context,
                                                                         std::vector<std::uint8_t>& buffer);

[[nodiscard]] SessionFailure readFailure(const ReadFailure& failure, std::string_view what);

[[nodiscard]] Expected<protocol::Welcome, SessionFailure> handshake(platform::TcpStream& stream, const Token& token,
                                                                    const std::string& clientVersion);
[[nodiscard]] Expected<void, SessionFailure> openChannel(platform::TcpStream& stream, const Token& token,
                                                         protocol::ChannelId channel);

}
