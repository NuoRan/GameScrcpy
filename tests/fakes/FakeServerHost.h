#pragma once

#include "FakeDeviceServer.h"

#include "gs/session/ServerHost.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace gs::session::test {

class FakeServerHost final : public ServerHost
{
public:
    explicit FakeServerHost(FakeDeviceServer& server) : m_server(server) {}

    Expected<void, SessionFailure> start(const LaunchParams& params, Events& events, std::stop_token) override
    {
        std::optional<SessionFailure> failure;
        SessionPhase failAt = SessionPhase::Idle;
        {
            const std::lock_guard lock(m_mutex);
            m_events = &events;
            m_params = params;
            failure = m_startFailure;
            failAt = m_failAt;
            m_server.setToken(m_serverToken ? *m_serverToken : params.token);
        }
        for (const SessionPhase phase : {SessionPhase::Deploying, SessionPhase::Launching, SessionPhase::Connecting}) {
            events.hostPhase(phase);
            if (failure && phase == failAt) {
                return makeUnexpected(*failure);
            }
        }
        return {};
    }

    Expected<platform::TcpStream, SessionFailure> connect() override
    {
        auto stream = platform::TcpStream::connectLoopback(m_server.port());
        if (!stream) {
            SessionFailure failure;
            failure.kind = FailureKind::ConnectFailed;
            failure.detail = "the fake server does not listen";
            return makeUnexpected(std::move(failure));
        }
        return std::move(*stream);
    }

    Expected<void, SessionFailure> restore(bool afterFailure, std::chrono::steady_clock::time_point deadline,
                                           std::stop_token stop) override
    {
        std::unique_lock lock(m_mutex);
        ++m_restores;
        m_changed.notify_all();
        if (afterFailure) {
            const std::uint64_t seen = m_reconnects;
            const bool changed =
                m_changed.wait_until(lock, stop, deadline, [&] { return m_reconnects != seen || m_ended; });
            if (!changed || m_ended) {
                SessionFailure failure;
                failure.kind = m_ended ? FailureKind::ServerEnded : FailureKind::ConnectionLost;
                failure.detail = m_ended ? "the server ended" : "the device did not come back";
                return makeUnexpected(std::move(failure));
            }
        }
        return {};
    }

    void stop(std::chrono::milliseconds) override
    {
        const std::lock_guard lock(m_mutex);
        ++m_stops;
    }

    void failStart(SessionPhase at, SessionFailure failure)
    {
        const std::lock_guard lock(m_mutex);
        m_failAt = at;
        m_startFailure = std::move(failure);
    }

    void useServerToken(const Token& token)
    {
        const std::lock_guard lock(m_mutex);
        m_serverToken = token;
    }

    void deviceReconnected()
    {
        {
            const std::lock_guard lock(m_mutex);
            ++m_reconnects;
        }
        m_changed.notify_all();
    }

    void endServer(SessionFailure failure)
    {
        Events* events = nullptr;
        {
            const std::lock_guard lock(m_mutex);
            events = m_events;
            m_ended = true;
        }
        if (events != nullptr) {
            events->serverEnded(std::move(failure));
        }
        m_changed.notify_all();
    }

    bool waitForRestores(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_restores >= count; });
    }

    [[nodiscard]] LaunchParams params() const
    {
        const std::lock_guard lock(m_mutex);
        return m_params;
    }

    [[nodiscard]] int restores() const
    {
        const std::lock_guard lock(m_mutex);
        return m_restores;
    }

    [[nodiscard]] int stops() const
    {
        const std::lock_guard lock(m_mutex);
        return m_stops;
    }

private:
    FakeDeviceServer& m_server;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;
    Events* m_events = nullptr;
    LaunchParams m_params;
    std::optional<SessionFailure> m_startFailure;
    SessionPhase m_failAt = SessionPhase::Idle;
    std::optional<Token> m_serverToken;
    std::uint64_t m_reconnects = 0;
    bool m_ended = false;
    int m_restores = 0;
    int m_stops = 0;
};

}
