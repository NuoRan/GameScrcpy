#pragma once

#include "gs/platform/UdpSocket.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <thread>

namespace gs::session::test {

class LossyRelay
{
public:
    LossyRelay(const platform::UdpEndpoint& server, double loss) : m_server(server), m_loss(loss)
    {
        m_up = std::jthread([this] { forward(m_clientSide, true); });
        m_down = std::jthread([this] { forward(m_serverSide, false); });
    }

    ~LossyRelay()
    {
        m_clientSide.interrupt();
        m_serverSide.interrupt();
    }

    LossyRelay(const LossyRelay&) = delete;
    LossyRelay& operator=(const LossyRelay&) = delete;

    [[nodiscard]] platform::UdpEndpoint endpoint() const { return platform::UdpEndpoint::loopback(m_clientSide.localPort()); }
    [[nodiscard]] int lostToClient(std::uint8_t channel) const { return m_lostToClient[channel & 0x0F].load(); }

private:
    void forward(platform::UdpSocket& from, bool up)
    {
        std::mt19937 random(up ? 11 : 12);
        std::array<std::uint8_t, 2048> buffer{};
        while (const auto received = from.receive(buffer)) {
            if (std::uniform_real_distribution<double>(0, 1)(random) < m_loss) {
                if (!up && received->size > 2) {
                    m_lostToClient[buffer[2] & 0x0F].fetch_add(1);
                }
                continue;
            }
            const auto datagram = std::span<const std::uint8_t>(buffer).first(received->size);
            std::optional<platform::UdpEndpoint> client;
            {
                const std::lock_guard lock(m_mutex);
                if (up) {
                    m_client = received->from;
                }
                client = m_client;
            }
            if (up) {
                static_cast<void>(m_serverSide.sendTo(m_server, datagram));
            } else if (client) {
                static_cast<void>(m_clientSide.sendTo(*client, datagram));
            }
        }
    }

    platform::UdpEndpoint m_server;
    double m_loss;
    platform::UdpSocket m_clientSide = *platform::UdpSocket::bind(platform::UdpEndpoint::loopback(0));
    platform::UdpSocket m_serverSide = *platform::UdpSocket::bind(platform::UdpEndpoint::any(m_server.v6, 0));
    std::mutex m_mutex;
    std::optional<platform::UdpEndpoint> m_client;
    std::array<std::atomic<int>, 16> m_lostToClient{};
    std::jthread m_up;
    std::jthread m_down;
};

}
