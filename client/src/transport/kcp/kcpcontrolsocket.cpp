/**
 * @file kcpcontrolsocket.cpp
 * @brief KCP Control Socket Implementation
 */

#include "kcpcontrolsocket.h"
#include "KcpClient.h"
#include "ThreadDispatcher.h"

KcpControlSocket::KcpControlSocket()
{
    m_client = new KcpControlClient();

    // Callbacks fire from IO thread -> marshal to main thread
    m_client->setConnectedCallback([this]() {
        dispatch::postToMain([this]() { connected.fire(); });
    });
    m_client->setDisconnectedCallback([this]() {
        dispatch::postToMain([this]() { disconnected.fire(); });
    });
    m_client->setErrorCallback([this](const std::string &err) {
        dispatch::postToMain([this, err]() { errorOccurred.fire(err); });
    });
    m_client->setDataReadyCallback([this]() {
        dispatch::postToMain([this]() { onDataReady(); });
    });
}

KcpControlSocket::~KcpControlSocket()
{
    close();
    delete m_client;
    m_client = nullptr;
}

bool KcpControlSocket::bind(uint16_t port)
{
    return m_client ? m_client->bind(port) : false;
}

uint16_t KcpControlSocket::localPort() const
{
    return m_client ? m_client->localPort() : 0;
}

std::string KcpControlSocket::localAddress() const
{
    return "0.0.0.0";
}

void KcpControlSocket::connectToHost(const std::string &host, uint16_t port)
{
    if (m_client) {
        m_client->connectTo(host, port);
    }
}

bool KcpControlSocket::isValid() const
{
    return m_client && m_client->isActive();
}

int64_t KcpControlSocket::write(const char *data, int64_t len)
{
    if (!m_client || len <= 0) return -1;
    int ret = m_client->send(data, static_cast<int>(len));
    return (ret >= 0) ? len : -1;
}

std::vector<uint8_t> KcpControlSocket::readAll()
{
    std::vector<uint8_t> result;
    result.swap(m_readBuffer);
    return result;
}

int64_t KcpControlSocket::bytesAvailable() const
{
    return static_cast<int64_t>(m_readBuffer.size());
}

void KcpControlSocket::close()
{
    if (m_client) {
        m_client->close();
    }
    disconnected.fire();
}

void KcpControlSocket::onDataReady()
{
    if (!m_client) return;

    std::vector<uint8_t> data = m_client->recv();
    if (!data.empty()) {
        m_readBuffer.insert(m_readBuffer.end(), data.begin(), data.end());
        readyRead.fire();
    }
}