/**
 * @file kcpvideosocket.cpp
 * @brief KCP视频Socket实现
 */

#include "kcpvideosocket.h"
#include "KcpClient.h"

KcpVideoSocket::KcpVideoSocket(QObject *parent)
    : QObject(parent)
{
    m_client = new KcpVideoClient(this);
    connect(m_client, &KcpVideoClient::connected, this, &KcpVideoSocket::connected);
    connect(m_client, &KcpVideoClient::disconnected, this, &KcpVideoSocket::disconnected);
    connect(m_client, &KcpVideoClient::errorOccurred, this, &KcpVideoSocket::errorOccurred);
}

KcpVideoSocket::~KcpVideoSocket()
{
}

void KcpVideoSocket::setBitrate(quint32 bitrateBps)
{
    if (m_client) {
        m_client->configureBitrate(bitrateBps);
    }
}

bool KcpVideoSocket::bind(quint16 port)
{
    return m_client ? m_client->bind(port) : false;
}

quint16 KcpVideoSocket::localPort() const
{
    return m_client ? m_client->localPort() : 0;
}

QHostAddress KcpVideoSocket::localAddress() const
{
    return QHostAddress::Any;
}

void KcpVideoSocket::connectToHost(const QHostAddress &host, quint16 port)
{
    if (m_client) {
        m_client->connectTo(host, port);
    }
}

bool KcpVideoSocket::isValid() const
{
    return m_client && m_client->isActive();
}

qint32 KcpVideoSocket::subThreadRecvData(quint8 *buf, qint32 bufSize)
{
    if (!m_client) {
        return 0;
    }
    return m_client->recvBlocking(reinterpret_cast<char *>(buf), bufSize);
}

void KcpVideoSocket::close()
{
    if (m_client) {
        m_client->close();
    }
    emit disconnected();
}

qint64 KcpVideoSocket::bytesAvailable() const
{
    return m_client ? m_client->available() : 0;
}

QString KcpVideoSocket::getStats() const
{
    return m_client ? m_client->stats() : QString();
}
