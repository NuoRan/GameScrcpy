/**
 * @file KcpClient.cpp
 * @brief KCP Client Implementation (no Qt dependency)
 */

#include "KcpClient.h"
#include <chrono>
#include <algorithm>

//=============================================================================
// KcpVideoClient
//=============================================================================

KcpVideoClient::KcpVideoClient()
{
    m_transport = new KcpTransport(CONV_VIDEO);

    // Video stream mode
    m_transport->setVideoStreamMode();

    // MTU: 1400 safe value, avoids fragmentation
    m_transport->setMtu(1400);

    // Pre-allocate ring buffer
    m_ringBuffer.reserve(DEFAULT_BUFFER_SIZE);

    // Set callbacks (dataReady fires from IO thread, same as DirectConnection before)
    m_transport->setDataReadyCallback([this]() { onDataReady(); });
    m_transport->setPeerConnectedCallback([this]() {
        if (m_connectedCb) m_connectedCb();
    });
    m_transport->setDisconnectedCallback([this]() {
        if (m_disconnectedCb) m_disconnectedCb();
    });
    m_transport->setErrorCallback([this](const std::string &err) {
        if (m_errorCb) m_errorCb(err);
    });
}

KcpVideoClient::~KcpVideoClient()
{
    close();
    delete m_transport;
    m_transport = nullptr;
}

void KcpVideoClient::configureBitrate(int bitrateBps)
{
    // Window = (bitrate/8) * (RTT/1000) / MSS
    // Assume RTT=50ms (WiFi typical), MSS=1376 (MTU-24 header)
    // 8Mbps: (8000000/8) * 0.05 / 1376 ~ 36, x4 for jitter headroom
    int windowSize = (bitrateBps / 8 * 200 / 1000) / 1376;
    windowSize = std::clamp(windowSize, 256, 4096);

    // Buffer size: 200ms of data
    m_maxBufferSize = std::clamp(bitrateBps / 8 / 5, 512 * 1024, 16 * 1024 * 1024);

    // Dynamic ring buffer resize
    m_ringBuffer.reserve(m_maxBufferSize);

    m_transport->setWindowSize(windowSize, windowSize);
}

bool KcpVideoClient::bind(uint16_t port)
{
    // KcpTransport::bind() starts its own IO thread internally
    return m_transport->bind(port);
}

uint16_t KcpVideoClient::localPort() const
{
    return m_transport->localPort();
}

void KcpVideoClient::connectTo(const std::string &host, uint16_t port)
{
    // KcpTransport::connectTo() starts IO thread if not running
    m_transport->connectTo(host, port);
}

bool KcpVideoClient::isActive() const
{
    return m_transport->isActive() && !m_closed;
}

int KcpVideoClient::recvBlocking(char *buf, int bufSize, int timeoutMs)
{
    if (!buf || bufSize <= 0 || m_closed) return 0;

    std::unique_lock<std::mutex> locker(m_mutex);
    while (m_ringBuffer.available() < bufSize) {
        if (m_closed) return 0;
        if (timeoutMs < 0) {
            m_dataAvailable.wait(locker);
        } else {
            auto status = m_dataAvailable.wait_for(locker, std::chrono::milliseconds(timeoutMs));
            if (status == std::cv_status::timeout && m_ringBuffer.available() < bufSize) return 0;
        }
    }

    int toRead = std::min(bufSize, m_ringBuffer.available());
    m_ringBuffer.read(buf, toRead);
    return toRead;
}

std::vector<uint8_t> KcpVideoClient::recv()
{
    if (!m_transport) return {};
    return m_transport->recv();
}

int KcpVideoClient::available() const
{
    std::lock_guard<std::mutex> locker(m_mutex);
    return m_ringBuffer.available();
}

void KcpVideoClient::close()
{
    m_closed = true;
    if (m_transport) {
        m_transport->close();
    }
    m_dataAvailable.notify_all();
}

std::string KcpVideoClient::stats() const
{
    char buf[128];
    snprintf(buf, sizeof(buf), "recv=%llu,buf=%d,pend=%d",
             m_totalRecv.load(), m_ringBuffer.available(),
             m_transport ? m_transport->pending() : 0);
    return buf;
}

void KcpVideoClient::onDataReady()
{
    if (!m_transport || m_closed) return;

    // Read all available KCP data in one batch
    static constexpr int RECV_BUFFER_SIZE = 64 * 1024;
    char recvBuffer[RECV_BUFFER_SIZE];

    int totalRecv = m_transport->core()->recvAll(recvBuffer, RECV_BUFFER_SIZE);
    if (totalRecv <= 0) return;

    m_totalRecv += totalRecv;

    {
        std::lock_guard<std::mutex> locker(m_mutex);

        // If insufficient space, drop oldest data (shouldn't happen on wired network)
        if (m_ringBuffer.freeSpace() < totalRecv) {
            int dropSize = totalRecv - m_ringBuffer.freeSpace();
            m_ringBuffer.drop(dropSize);
        }

        m_ringBuffer.write(recvBuffer, totalRecv);
    }
    m_dataAvailable.notify_all();
}

//=============================================================================
// KcpControlClient
//=============================================================================

KcpControlClient::KcpControlClient()
{
    m_transport = new KcpTransport(CONV_CONTROL);

    // Message mode: preserve message boundaries
    m_transport->setStreamMode(0);

    // Window: 64 is enough for control messages
    m_transport->setWindowSize(64, 64);

    // Min RTO: control messages are latency-sensitive
    m_transport->setMinRto(1);

    // Set callbacks
    m_transport->setDataReadyCallback([this]() { onDataReady(); });
    m_transport->setPeerConnectedCallback([this]() {
        if (m_connectedCb) m_connectedCb();
    });
    m_transport->setDisconnectedCallback([this]() {
        if (m_disconnectedCb) m_disconnectedCb();
    });
    m_transport->setErrorCallback([this](const std::string &err) {
        if (m_errorCb) m_errorCb(err);
    });
}

KcpControlClient::~KcpControlClient()
{
    close();
    delete m_transport;
    m_transport = nullptr;
}

bool KcpControlClient::bind(uint16_t port)
{
    return m_transport->bind(port);
}

uint16_t KcpControlClient::localPort() const
{
    return m_transport->localPort();
}

void KcpControlClient::connectTo(const std::string &host, uint16_t port)
{
    m_transport->connectTo(host, port);
}

bool KcpControlClient::isActive() const
{
    return m_transport && m_transport->isActive() && !m_closed;
}

int KcpControlClient::send(const char *data, int len)
{
    return m_transport->send(data, len);
}

int KcpControlClient::recvBlocking(char *buf, int bufSize, int timeoutMs)
{
    if (!buf || bufSize <= 0 || m_closed) return 0;

    std::unique_lock<std::mutex> locker(m_mutex);
    while (m_buffer.empty()) {
        if (m_closed) return 0;
        if (timeoutMs < 0) {
            m_dataAvailable.wait(locker);
        } else {
            auto status = m_dataAvailable.wait_for(locker, std::chrono::milliseconds(timeoutMs));
            if (status == std::cv_status::timeout && m_buffer.empty()) return 0;
        }
    }

    int toRead = std::min(bufSize, static_cast<int>(m_buffer.size()));
    memcpy(buf, m_buffer.data(), toRead);
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + toRead);
    return toRead;
}

std::vector<uint8_t> KcpControlClient::recv()
{
    std::lock_guard<std::mutex> locker(m_mutex);
    if (m_buffer.empty()) return {};
    std::vector<uint8_t> result;
    result.swap(m_buffer);
    return result;
}

void KcpControlClient::close()
{
    m_closed = true;
    if (m_transport) m_transport->close();
    m_dataAvailable.notify_all();
}

void KcpControlClient::onDataReady()
{
    if (!m_transport || m_closed) return;

    while (m_transport->peekSize() > 0) {
        std::vector<uint8_t> data = m_transport->recv();
        if (data.empty()) break;

        {
            std::lock_guard<std::mutex> locker(m_mutex);
            m_buffer.insert(m_buffer.end(), data.begin(), data.end());
        }
        m_dataAvailable.notify_all();
    }

    if (m_dataReadyCb) m_dataReadyCb();
}