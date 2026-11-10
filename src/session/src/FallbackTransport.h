#pragma once

#include "gs/base/Log.h"
#include "gs/session/Transport.h"

#include <memory>
#include <stop_token>
#include <string>
#include <utility>

namespace gs::session::detail {

class FallbackTransport final : public Transport
{
public:
    FallbackTransport(std::unique_ptr<Transport> direct, std::unique_ptr<Transport> tunnel, std::string id)
        : m_direct(std::move(direct)), m_tunnel(std::move(tunnel)), m_id(std::move(id))
    {
    }

    [[nodiscard]] Expected<std::unique_ptr<Link>, SessionFailure> establish(const Token& token,
                                                                            const std::string& clientVersion,
                                                                            std::stop_token stop) override
    {
        auto link = m_direct->establish(token, clientVersion, stop);
        if (link || stop.stop_requested()) {
            return link;
        }
        log::info("session", "[{}] WiFi direct failed ({}); using the adb tunnel", m_id, link.error().detail);
        return m_tunnel->establish(token, clientVersion, stop);
    }

private:
    std::unique_ptr<Transport> m_direct;
    std::unique_ptr<Transport> m_tunnel;
    std::string m_id;
};

}
