/**
 * @file KcpTransport.cpp
 * @brief KCP传输层实现
 */

#include "KcpTransport.h"
#include <QHostAddress>

// Windows 高精度定时器
#ifdef Q_OS_WIN
#include <qt_windows.h>  // Windows 基础类型（UINT, DWORD 等），timeapi.h 依赖
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#include <QVariant>

// UDP socket 缓冲区大小
static constexpr int UDP_RECV_BUFFER_SIZE = 2 * 1024 * 1024;  // 2MB - 防止 OS 层丢包
static constexpr int UDP_SEND_BUFFER_SIZE = 1 * 1024 * 1024;  // 1MB

KcpTransport::KcpTransport(uint32_t conv, QObject *parent)
    : QObject(parent)
{
    m_kcp = std::make_unique<KcpCore>(conv, nullptr);
    m_kcp->setOutput([this](const char *buf, int len, void *) {
        return this->udpOutput(buf, len);
    });
    m_kcp->setFastMode();

    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &KcpTransport::onSocketReadyRead);

    m_updateTimer = new QTimer(this);
    m_updateTimer->setTimerType(Qt::PreciseTimer);
    connect(m_updateTimer, &QTimer::timeout, this, &KcpTransport::onUpdateTimer);

    m_clock.start();

    // Windows 默认定时器精度 15.6ms，timeBeginPeriod(1) 提升至 1ms 以保证 KCP 重传和流控的及时性
#ifdef Q_OS_WIN
    timeBeginPeriod(1);
#endif
}

KcpTransport::~KcpTransport()
{
    close();
#ifdef Q_OS_WIN
    timeEndPeriod(1);
#endif
}

bool KcpTransport::bind(quint16 port)
{
    if (!m_socket->bind(QHostAddress::Any, port)) {
        emit errorOccurred(m_socket->errorString());
        return false;
    }

    // 扩大 UDP 缓冲区，防止高码率时 OS 丢包导致 KCP 重传
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, QVariant(UDP_RECV_BUFFER_SIZE));
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, QVariant(UDP_SEND_BUFFER_SIZE));

    m_updateTimer->start(m_updateInterval);
    m_active = true;
    return true;
}

void KcpTransport::connectTo(const QHostAddress &address, quint16 port)
{
    m_remoteAddress = address;
    m_remotePort = port;

    if (m_socket->state() != QAbstractSocket::BoundState) {
        m_socket->bind();
    }

    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, QVariant(UDP_RECV_BUFFER_SIZE));
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, QVariant(UDP_SEND_BUFFER_SIZE));

    if (!m_updateTimer->isActive()) {
        m_updateTimer->start(m_updateInterval);
    }
    m_active = true;
    emit peerConnected();
}

void KcpTransport::close()
{
    m_active = false;
    if (m_updateTimer) m_updateTimer->stop();
    if (m_socket) m_socket->close();
    emit disconnected();
}

quint16 KcpTransport::localPort() const
{
    return m_socket ? m_socket->localPort() : 0;
}

int KcpTransport::send(const char *data, int len)
{
    if (!m_kcp || !m_active || len <= 0) return -1;
    int ret = m_kcp->send(data, len);
    // 立即flush，减少延迟
    if (ret >= 0) {
        m_kcp->update(currentMs());
    }
    return ret;
}

int KcpTransport::send(const QByteArray &data)
{
    return send(data.constData(), data.size());
}

QByteArray KcpTransport::recv()
{
    if (!m_kcp) return QByteArray();

    int size = m_kcp->peekSize();
    if (size <= 0) return QByteArray();

    QByteArray data(size, Qt::Uninitialized);  // 不零初始化，后续 ikcp_recv 会完全覆写
    int ret = m_kcp->recv(data.data(), size);
    if (ret < 0) return QByteArray();

    data.resize(ret);
    return data;
}

int KcpTransport::peekSize() const
{
    return m_kcp ? m_kcp->peekSize() : -1;
}

int KcpTransport::pending() const
{
    return m_kcp ? m_kcp->waitSnd() : 0;
}

void KcpTransport::setFastMode()
{
    if (m_kcp) m_kcp->setFastMode();
}

void KcpTransport::setVideoStreamMode()
{
    if (m_kcp) m_kcp->setVideoStreamMode();
}

void KcpTransport::setNormalMode()
{
    if (m_kcp) m_kcp->setNormalMode();
}

void KcpTransport::setDefaultMode()
{
    if (m_kcp) m_kcp->setDefaultMode();
}

void KcpTransport::setWindowSize(int sndwnd, int rcvwnd)
{
    if (m_kcp) m_kcp->setWindowSize(sndwnd, rcvwnd);
}

void KcpTransport::setMtu(int mtu)
{
    if (m_kcp) m_kcp->setMtu(mtu);
}

void KcpTransport::setUpdateInterval(int interval)
{
    m_updateInterval = qBound(1, interval, 100);
    if (m_updateTimer && m_updateTimer->isActive()) {
        m_updateTimer->setInterval(m_updateInterval);
    }
}

void KcpTransport::setNoDelay(int nodelay, int interval, int resend, int nc)
{
    if (m_kcp) m_kcp->setNoDelay(nodelay, interval, resend, nc);
}

void KcpTransport::setMinRto(int minrto)
{
    if (m_kcp) m_kcp->setMinRto(minrto);
}

void KcpTransport::setStreamMode(int stream)
{
    if (m_kcp) m_kcp->setStream(stream);
}

void KcpTransport::setFecEnabled(bool enabled, int groupSize)
{
    m_fecEnabled = enabled;
    if (enabled) {
        m_fecEncoder = std::make_unique<fec::FecEncoder>(groupSize);
        m_fecDecoder = std::make_unique<fec::FecDecoder>();
        qInfo("[KcpTransport] FEC enabled: groupSize=%d", groupSize);
    } else {
        m_fecEncoder.reset();
        m_fecDecoder.reset();
        qInfo("[KcpTransport] FEC disabled");
    }
}

void KcpTransport::onSocketReadyRead()
{
    // 批量收集 UDP 包后一次性输入 KCP，减少加锁开销
    struct UdpPacket {
        char data[1500];  // MTU 上限
        int size;
    };
    static constexpr int MAX_BATCH = 64;
    UdpPacket packets[MAX_BATCH];
    const char* ptrs[MAX_BATCH];
    int sizes[MAX_BATCH];
    int count = 0;

    QHostAddress sender;
    quint16 senderPort;

    while (m_socket->hasPendingDatagrams() && count < MAX_BATCH) {
        qint64 size = m_socket->readDatagram(packets[count].data, sizeof(packets[count].data),
                                              &sender, &senderPort);
        if (size <= 0) continue;

        if (m_remotePort == 0) {
            m_remoteAddress = sender;
            m_remotePort = senderPort;
            emit peerConnected();
        }

        packets[count].size = static_cast<int>(size);
        ptrs[count] = packets[count].data;
        sizes[count] = packets[count].size;
        count++;
    }

    if (count == 0) return;

    // FEC 解码或批量输入
    bool hasData = false;
    if (m_fecEnabled && m_fecDecoder) {
        // FEC 模式：解码后逐个输入（FEC 可能产生额外恢复包）
        for (int i = 0; i < count; ++i) {
            m_fecDecoder->decode(reinterpret_cast<const uint8_t*>(ptrs[i]), sizes[i],
                [this](const uint8_t* data, int dataLen) {
                    m_kcp->input(reinterpret_cast<const char*>(data), dataLen);
                });
        }
        m_kcp->update(currentMs());
        hasData = m_kcp->peekSize() > 0;
    } else {
        // 非 FEC 模式：批量输入 + update（单次加锁）
        // processInputBatch 内部已做 peekSize，直接利用返回值，省去额外一次加锁
        hasData = m_kcp->processInputBatch(ptrs, sizes, count, currentMs()) > 0;
    }

    if (hasData) {
        emit dataReady();
    }
}

void KcpTransport::onUpdateTimer()
{
    if (!m_kcp || !m_active) return;

    uint32_t current = currentMs();
    m_kcp->update(current);

    // 使用 ikcp_check 计算下次需要更新的时间
    scheduleNextUpdate();
}

void KcpTransport::scheduleNextUpdate()
{
    if (!m_kcp || !m_active || !m_updateTimer) return;

    uint32_t current = currentMs();
    uint32_t next = m_kcp->check(current);

    // 计算下次更新的延迟时间
    int delay = static_cast<int>(next - current);
    delay = qBound(1, delay, 100);  // 限制在 1-100ms 范围内

    // 动态调整 timer 间隔
    // P-KCP: 添加阈值容差，避免微小变化时频繁重注册底层定时器
    if (qAbs(m_updateTimer->interval() - delay) > 2) {
        m_updateTimer->setInterval(delay);
    }
}

int KcpTransport::udpOutput(const char *buf, int len)
{
    if (!m_socket || !m_active || m_remotePort == 0) return -1;

    // FEC 编码：每 groupSize 个包生成 1 个 XOR 校验包，可恢复单个丢包
    if (m_fecEnabled && m_fecEncoder) {
        m_fecEncoder->encode(reinterpret_cast<const uint8_t*>(buf), len,
            [this](const uint8_t* data, int dataLen) {
                m_socket->writeDatagram(reinterpret_cast<const char*>(data), dataLen,
                                        m_remoteAddress, m_remotePort);
            });
        return len;
    }

    qint64 sent = m_socket->writeDatagram(buf, len, m_remoteAddress, m_remotePort);
    return sent < 0 ? -1 : static_cast<int>(sent);
}

uint32_t KcpTransport::currentMs() const
{
    return static_cast<uint32_t>(m_clock.elapsed());
}
