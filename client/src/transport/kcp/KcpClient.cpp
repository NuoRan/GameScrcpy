/**
 * @file KcpClient.cpp
 * @brief KCP客户端实现
 */

#include "KcpClient.h"

//=============================================================================
// KcpVideoClient
//=============================================================================

KcpVideoClient::KcpVideoClient(QObject *parent)
    : QObject(parent)
{
    m_transport = new KcpTransport(CONV_VIDEO, this);
    // C-K04: 预分配缓冲区，避免频繁重新分配
    m_buffer.reserve(DEFAULT_BUFFER_SIZE);
    connect(m_transport, &KcpTransport::dataReady, this, &KcpVideoClient::onDataReady);
    connect(m_transport, &KcpTransport::peerConnected, this, &KcpVideoClient::connected);
    connect(m_transport, &KcpTransport::disconnected, this, &KcpVideoClient::disconnected);
    connect(m_transport, &KcpTransport::errorOccurred, this, &KcpVideoClient::errorOccurred);
}

KcpVideoClient::~KcpVideoClient()
{
    close();
}

void KcpVideoClient::configureBitrate(int bitrateBps)
{
    int windowSize = (bitrateBps / 8 * 200 / 1000) / 1376;
    windowSize = qBound(128, windowSize, 2048);
    m_maxBufferSize = qBound(512 * 1024, bitrateBps / 8 / 2, 8 * 1024 * 1024);
    m_transport->setWindowSize(windowSize, windowSize);
}

bool KcpVideoClient::bind(quint16 port)
{
    return m_transport->bind(port);
}

quint16 KcpVideoClient::localPort() const
{
    return m_transport->localPort();
}

void KcpVideoClient::connectTo(const QHostAddress &host, quint16 port)
{
    m_transport->connectTo(host, port);
}

bool KcpVideoClient::isActive() const
{
    return m_transport->isActive() && !m_closed;
}

int KcpVideoClient::recvBlocking(char *buf, int bufSize, int timeoutMs)
{
    if (!buf || bufSize <= 0 || m_closed) return 0;

    QMutexLocker locker(&m_mutex);
    while (m_buffer.size() < bufSize) {
        if (m_closed) return 0;
        bool ok = timeoutMs < 0 ? m_dataAvailable.wait(&m_mutex)
                                : m_dataAvailable.wait(&m_mutex, timeoutMs);
        if (!ok && m_buffer.size() < bufSize) return 0;
    }

    int toRead = qMin(bufSize, static_cast<int>(m_buffer.size()));
    memcpy(buf, m_buffer.constData(), toRead);
    m_buffer.remove(0, toRead);
    return toRead;
}

QByteArray KcpVideoClient::recv()
{
    return m_transport->recv();
}

int KcpVideoClient::available() const
{
    QMutexLocker locker(&m_mutex);
    return m_buffer.size();
}

void KcpVideoClient::close()
{
    m_closed = true;
    if (m_transport) m_transport->close();
    m_dataAvailable.wakeAll();
}

QString KcpVideoClient::stats() const
{
    return QString("recv=%1,buf=%2,pend=%3")
        .arg(m_totalRecv.load()).arg(m_buffer.size())
        .arg(m_transport ? m_transport->pending() : 0);
}

void KcpVideoClient::onDataReady()
{
    if (!m_transport || m_closed) return;

    while (m_transport->peekSize() > 0) {
        QByteArray data = m_transport->recv();
        if (data.isEmpty()) break;

        m_totalRecv += data.size();
        QMutexLocker locker(&m_mutex);

        if (m_buffer.size() + data.size() > m_maxBufferSize) {
            int dropSize = (m_buffer.size() + data.size()) - m_maxBufferSize;
            m_buffer.remove(0, dropSize);
        }

        m_buffer.append(data);
        m_dataAvailable.wakeAll();
    }
}

//=============================================================================
// KcpControlClient
//=============================================================================

KcpControlClient::KcpControlClient(QObject *parent)
    : QObject(parent)
{
    m_transport = new KcpTransport(CONV_CONTROL, this);
    m_transport->setStreamMode(0);  // 控制通道使用消息模式（保持消息边界）
    m_transport->setWindowSize(64, 64);
    connect(m_transport, &KcpTransport::dataReady, this, &KcpControlClient::onDataReady);
    connect(m_transport, &KcpTransport::peerConnected, this, &KcpControlClient::connected);
    connect(m_transport, &KcpTransport::disconnected, this, &KcpControlClient::disconnected);
    connect(m_transport, &KcpTransport::errorOccurred, this, &KcpControlClient::errorOccurred);
}

KcpControlClient::~KcpControlClient()
{
    close();
}

bool KcpControlClient::bind(quint16 port)
{
    return m_transport->bind(port);
}

quint16 KcpControlClient::localPort() const
{
    return m_transport->localPort();
}

void KcpControlClient::connectTo(const QHostAddress &host, quint16 port)
{
    m_transport->connectTo(host, port);
}

bool KcpControlClient::isActive() const
{
    return m_transport && m_transport->isActive() && !m_closed;
}

int KcpControlClient::send(const QByteArray &data)
{
    return m_transport->send(data);
}

int KcpControlClient::send(const char *data, int len)
{
    return m_transport->send(data, len);
}

int KcpControlClient::recvBlocking(char *buf, int bufSize, int timeoutMs)
{
    if (!buf || bufSize <= 0 || m_closed) return 0;

    QMutexLocker locker(&m_mutex);
    while (m_buffer.isEmpty()) {
        if (m_closed) return 0;
        bool ok = timeoutMs < 0 ? m_dataAvailable.wait(&m_mutex)
                                : m_dataAvailable.wait(&m_mutex, timeoutMs);
        if (!ok && m_buffer.isEmpty()) return 0;
    }

    int toRead = qMin(bufSize, static_cast<int>(m_buffer.size()));
    memcpy(buf, m_buffer.constData(), toRead);
    m_buffer.remove(0, toRead);
    return toRead;
}

QByteArray KcpControlClient::recv()
{
    return m_transport->recv();
}

void KcpControlClient::close()
{
    m_closed = true;
    if (m_transport) m_transport->close();
    m_dataAvailable.wakeAll();
}

void KcpControlClient::onDataReady()
{
    if (!m_transport || m_closed) return;

    while (m_transport->peekSize() > 0) {
        QByteArray data = m_transport->recv();
        if (data.isEmpty()) break;

        QMutexLocker locker(&m_mutex);
        m_buffer.append(data);
        m_dataAvailable.wakeAll();
    }

    emit dataReady();
}
