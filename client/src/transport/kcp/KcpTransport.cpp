/**
 * @file KcpTransport.cpp
 * @brief KCP传输层实现
 */

#include "KcpTransport.h"
#include "PerformanceMonitor.h"
#include <QHostAddress>

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
}

KcpTransport::~KcpTransport()
{
    close();
}

bool KcpTransport::bind(quint16 port)
{
    if (!m_socket->bind(QHostAddress::Any, port)) {
        emit errorOccurred(m_socket->errorString());
        return false;
    }

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
        // 报告发送字节数
        qsc::PerformanceMonitor::instance().reportBytesSent(len);
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

void KcpTransport::onSocketReadyRead()
{
    // P-KCP: 使用 readDatagram + 预分配栈缓冲区，避免每个 UDP 包创建 QNetworkDatagram 堆对象
    char buf[2048];  // MTU 通常 < 1500
    QHostAddress sender;
    quint16 senderPort;

    while (m_socket->hasPendingDatagrams()) {
        qint64 size = m_socket->readDatagram(buf, sizeof(buf), &sender, &senderPort);
        if (size <= 0) continue;

        // 报告接收字节数
        qsc::PerformanceMonitor::instance().reportBytesReceived(static_cast<int>(size));

        if (m_remotePort == 0) {
            m_remoteAddress = sender;
            m_remotePort = senderPort;
            emit peerConnected();
        }

        m_kcp->input(buf, static_cast<int>(size));
    }

    // 【关键优化】收到数据后立即update，让ACK最快发出
    // 这避免了对端因为收不到ACK而触发重传
    m_kcp->update(currentMs());

    if (m_kcp->peekSize() > 0) {
        emit dataReady();
    }
}

void KcpTransport::onUpdateTimer()
{
    if (!m_kcp || !m_active) return;

    uint32_t current = currentMs();
    m_kcp->update(current);

    // 报告网络延迟（RTT）
    int rtt = m_kcp->getRtt();
    if (rtt > 0) {
        qsc::PerformanceMonitor::instance().reportNetworkLatency(rtt);
    }

    // 【按需调度优化】使用 ikcp_check 计算下次需要更新的时间
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
    qint64 sent = m_socket->writeDatagram(buf, len, m_remoteAddress, m_remotePort);
    return sent < 0 ? -1 : static_cast<int>(sent);
}

uint32_t KcpTransport::currentMs() const
{
    return static_cast<uint32_t>(m_clock.elapsed());
}
