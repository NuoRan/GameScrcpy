#pragma once

#include "gs/session/ServerHost.h"
#include "gs/session/StreamHost.h"
#include "gs/session/Transport.h"

#include <chrono>
#include <memory>
#include <stop_token>
#include <string>

namespace gs::session::detail {

class StreamTransport final : public Transport
{
public:
    StreamTransport(std::shared_ptr<PhoneStreams> streams, LaunchParams params,
                    std::chrono::milliseconds handshakeTimeout) noexcept;

    [[nodiscard]] Expected<std::unique_ptr<Link>, SessionFailure> establish(const Token& token,
                                                                            const std::string& clientVersion,
                                                                            std::stop_token stop) override;

private:
    std::shared_ptr<PhoneStreams> m_streams;
    LaunchParams m_params;
    std::chrono::milliseconds m_handshakeTimeout;
};

}
