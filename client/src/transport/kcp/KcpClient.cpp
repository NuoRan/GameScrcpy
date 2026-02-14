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

    // 视频流模式
    m_transport->setVideoStreamMode();

    // MTU优化：1400是安全值，避免分片
    m_transport->setMtu(1400);

    // 环形缓冲区预分配
    m_ringBuffer.reserve(DEFAULT_BUFFER_SIZE);
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
    // 【窗口大小计算】
    // 公式: 窗口 = (码率/8) * (RTT/1000) / MSS
    // 假设RTT=50ms（WiFi典型值），MSS=1376（MTU-24头）
    // 8Mbps: (8000000/8) * 0.05 / 1376 ≈ 36
    // 为了应对抖动，乘以4倍余量
    int windowSize = (bitrateBps / 8 * 200 / 1000) / 1376;
    windowSize = qBound(256, windowSize, 4096);  // 最小256，支持突发

    // 缓冲区大小：200ms的数据量
    m_maxBufferSize = qBound(512 * 1024, bitrateBps / 8 / 5, 16 * 1024 * 1024);

    // 环形缓冲区动态扩容
    m_ringBuffer.reserve(m_maxBufferSize);

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
    // 环形缓冲区 O(1) 读取
    while (m_ringBuffer.available() < bufSize) {
        if (m_closed) return 0;
        bool ok = timeoutMs < 0 ? m_dataAvailable.wait(&m_mutex)
                                : m_dataAvailable.wait(&m_mutex, timeoutMs);
        if (!ok && m_ringBuffer.available() < bufSize) return 0;
    }

    int toRead = qMin(bufSize, m_ringBuffer.available());
    m_ringBuffer.read(buf, toRead);
    return toRead;
}

QByteArray KcpVideoClient::recv()
{
    return m_transport->recv();
}

int KcpVideoClient::available() const
{
    QMutexLocker locker(&m_mutex);
    return m_ringBuffer.available();
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
        .arg(m_totalRecv.load()).arg(m_ringBuffer.available())
        .arg(m_transport ? m_transport->pending() : 0);
}

void KcpVideoClient::onDataReady()
{
    if (!m_transport || m_closed) return;

    // P-KCP: 先在锁外收集所有数据，然后一次加锁、一次写入、一次唤醒
    QByteArray batch;
    while (m_transport->peekSize() > 0) {
        QByteArray data = m_transport->recv();
        if (data.isEmpty()) break;
        m_totalRecv += data.size();
        batch.append(data);
    }

    if (batch.isEmpty()) return;

    QMutexLocker locker(&m_mutex);

    // 环形缓冲区 O(1) 写入
    // 如果空间不足，丢弃最旧的数据
    if (m_ringBuffer.freeSpace() < batch.size()) {
        int dropSize = batch.size() - m_ringBuffer.freeSpace();
        m_ringBuffer.drop(dropSize);
    }

    m_ringBuffer.write(batch.constData(), batch.size());
    m_dataAvailable.wakeAll();
}

//=============================================================================
// KcpControlClient
//=============================================================================

KcpControlClient::KcpControlClient(QObject *parent)
    : QObject(parent)
{
    m_transport = new KcpTransport(CONV_CONTROL, this);

    // 消息模式：保持消息边界
    m_transport->setStreamMode(0);

    // 2. 窗口大小：控制消息小，64足够
    m_transport->setWindowSize(64, 64);

    // 3. 最小RTO：控制消息对延迟敏感
    m_transport->setMinRto(1);

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
    // P-KCP: 从 m_buffer 读取，修复与 onDataReady() 数据竞争的 bug
    // （旧代码直接从 m_transport->recv()，但数据已被 onDataReady 消费）
    QMutexLocker locker(&m_mutex);
    if (m_buffer.isEmpty()) return QByteArray();
    QByteArray result;
    result.swap(m_buffer);
    return result;
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
