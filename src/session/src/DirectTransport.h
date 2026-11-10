#pragma once

#include "gs/platform/UdpSocket.h"
#include "gs/session/Transport.h"

#include <memory>
#include <stop_token>
#include <string>

namespace gs::session::detail {

class DirectTransport final : public Transport
{
public:
    explicit DirectTransport(const platform::UdpEndpoint& server) noexcept : m_server(server) {}

    [[nodiscard]] Expected<std::unique_ptr<Link>, SessionFailure> establish(const Token& token,
                                                                            const std::string& clientVersion,
                                                                            std::stop_token stop) override;

private:
    platform::UdpEndpoint m_server;
};

}
