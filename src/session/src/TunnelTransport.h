#pragma once

#include "gs/session/ServerHost.h"
#include "gs/session/Transport.h"

#include <chrono>
#include <memory>
#include <stop_token>
#include <string>

namespace gs::session::detail {

class TunnelTransport final : public Transport
{
public:
    TunnelTransport(ServerHost& host, std::chrono::milliseconds handshakeTimeout) noexcept;

    [[nodiscard]] Expected<std::unique_ptr<Link>, SessionFailure> establish(const Token& token,
                                                                            const std::string& clientVersion,
                                                                            std::stop_token stop) override;

private:
    ServerHost& m_host;
    std::chrono::milliseconds m_handshakeTimeout;
};

}
