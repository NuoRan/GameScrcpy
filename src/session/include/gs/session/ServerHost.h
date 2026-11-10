#pragma once

#include "gs/base/Expected.h"
#include "gs/platform/TcpStream.h"
#include "gs/session/SessionState.h"
#include "gs/session/Transport.h"

#include <chrono>
#include <memory>
#include <stop_token>
#include <string>

namespace gs::session {

struct LaunchParams {
    Token token{};
    std::string socketName;
    std::chrono::milliseconds linger{15000};
    std::string logLevel = "info";
};

class ServerHost
{
public:
    class Events
    {
    public:
        virtual void hostPhase(SessionPhase phase) = 0;
        virtual void serverEnded(SessionFailure failure) = 0;

    protected:
        ~Events() = default;
    };

    virtual ~ServerHost() = default;

    [[nodiscard]] virtual Expected<void, SessionFailure> start(const LaunchParams& params, Events& events,
                                                               std::stop_token stop) = 0;
    [[nodiscard]] virtual Expected<platform::TcpStream, SessionFailure> connect() = 0;
    [[nodiscard]] virtual std::unique_ptr<Transport> transport(std::chrono::milliseconds handshakeTimeout);
    [[nodiscard]] virtual Expected<void, SessionFailure> restore(bool afterFailure,
                                                                 std::chrono::steady_clock::time_point deadline,
                                                                 std::stop_token stop) = 0;
    virtual void stop(std::chrono::milliseconds grace) = 0;
};

}
