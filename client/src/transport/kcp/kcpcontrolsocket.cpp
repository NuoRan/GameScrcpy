/**
 * @file kcpcontrolsocket.cpp
 * @brief KCP控制Socket实现
 */

#include "kcpcontrolsocket.h"
#include "KcpClient.h"

KcpControlSocket::KcpControlSocket(QObject *parent)
    : QObject(parent)
{
    m_client = new KcpControlClient(this);
    connect(m_client, &KcpControlClient::connected, this, &KcpControlSocket::connected);
    connect(m_client, &KcpControlClient::disconnected, this, &KcpControlSocket::disconnected);
    connect(m_client, &KcpControlClient::errorOccurred, this, &KcpControlSocket::errorOccurred);
    connect(m_client, &KcpControlClient::dataReady, this, &KcpControlSocket::onDataReady);
}

KcpControlSocket::~KcpControlSocket()
{
}

bool KcpControlSocket::bind(quint16 port)
{
    return m_client ? m_client->bind(port) : false;
}

quint16 KcpControlSocket::localPort() const
{
    return m_client ? m_client->localPort() : 0;
}

QHostAddress KcpControlSocket::localAddress() const
{
    return QHostAddress::Any;
}

void KcpControlSocket::connectToHost(const QHostAddress &host, quint16 port)
{
    if (m_client) {
        m_client->connectTo(host, port);
    }
}

bool KcpControlSocket::isValid() const
{
    return m_client && m_client->isActive();
}

qint64 KcpControlSocket::write(const char *data, qint64 len)
{
    if (!m_client || len <= 0) {
        return -1;
    }
    int ret = m_client->send(data, static_cast<int>(len));
    return (ret >= 0) ? len : -1;
}

qint64 KcpControlSocket::write(const QByteArray &data)
{
    return write(data.constData(), data.size());
}

QByteArray KcpControlSocket::readAll()
{
    // P-KCP: move 语义避免 COW detach 拷贝
    QByteArray result;
    result.swap(m_readBuffer);
    return result;
}

qint64 KcpControlSocket::bytesAvailable() const
{
    return m_readBuffer.size();
}

void KcpControlSocket::close()
{
    if (m_client) {
        m_client->close();
    }
    emit disconnected();
}

void KcpControlSocket::onDataReady()
{
    if (!m_client) return;

    // 从客户端接收数据并缓存
    QByteArray data = m_client->recv();
    if (!data.isEmpty()) {
        m_readBuffer.append(data);
        emit readyRead();
    }
}
