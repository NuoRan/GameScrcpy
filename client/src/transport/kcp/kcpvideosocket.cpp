/**
 * @file kcpvideosocket.cpp
 * @brief UDP Video Socket Implementation
 */

#include "kcpvideosocket.h"
#include "UdpVideoClient.h"
#include "ThreadDispatcher.h"

KcpVideoSocket::KcpVideoSocket()
{
    m_client = new UdpVideoClient();

    // Callbacks fire from IO thread -> marshal to main thread via dispatch::postToMain
    m_client->setConnectedCallback([this]() {
        dispatch::postToMain([this]() { connected.fire(); });
    });
    m_client->setDisconnectedCallback([this]() {
        dispatch::postToMain([this]() { disconnected.fire(); });
    });
    m_client->setErrorCallback([this](const std::string &err) {
        dispatch::postToMain([this, err]() { errorOccurred.fire(err); });
    });
}

KcpVideoSocket::~KcpVideoSocket()
{
    close();
    delete m_client;
    m_client = nullptr;
}

void KcpVideoSocket::setBitrate(uint32_t bitrateBps, uint32_t maxFps)
{
    if (m_client) {
        m_client->configure(bitrateBps, maxFps);
    }
}

bool KcpVideoSocket::bind(uint16_t port)
{
    return m_client ? m_client->bind(port) : false;
}

uint16_t KcpVideoSocket::localPort() const
{
    return m_client ? m_client->localPort() : 0;
}

std::string KcpVideoSocket::localAddress() const
{
    return "0.0.0.0";
}

void KcpVideoSocket::connectToHost(const std::string &host, uint16_t port)
{
    if (m_client) {
        m_client->connectTo(host, port);
    }
}

bool KcpVideoSocket::isValid() const
{
    return m_client && m_client->isActive();
}

int32_t KcpVideoSocket::subThreadRecvData(uint8_t *buf, int32_t bufSize)
{
    if (!m_client) return 0;
    return m_client->recvBlocking(reinterpret_cast<char *>(buf), bufSize);
}

void KcpVideoSocket::close()
{
    if (m_client) {
        m_client->close();
    }
    disconnected.fire();
}

int64_t KcpVideoSocket::bytesAvailable() const
{
    return m_client ? m_client->available() : 0;
}

std::string KcpVideoSocket::getStats() const
{
    return m_client ? m_client->stats() : std::string();
}