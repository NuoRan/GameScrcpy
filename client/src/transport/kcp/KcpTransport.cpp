/**
 * @file KcpTransport.cpp
 * @brief KCP传输层实现
 */

#include "KcpTransport.h"
#include <QNetworkDatagram>

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
    // C-K03: 移除立即flush，由定时器的update()自动flush，减少不必要的发送
    return m_kcp->send(data, len);
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

    QByteArray data(size, 0);
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
    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        if (datagram.data().isEmpty()) continue;

        if (m_remotePort == 0) {
            m_remoteAddress = datagram.senderAddress();
            m_remotePort = datagram.senderPort();
            emit peerConnected();
        }

        m_kcp->input(datagram.data().constData(), datagram.data().size());
    }

    if (m_kcp->peekSize() > 0) {
        emit dataReady();
    }
}

void KcpTransport::onUpdateTimer()
{
    if (!m_kcp || !m_active) return;
    m_kcp->update(currentMs());
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
