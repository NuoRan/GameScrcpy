#include "TcpControlChannel.h"
#include "fastmsg.h"
#include <QHostAddress>

namespace qsc {
namespace core {

TcpControlChannel::TcpControlChannel()
    : m_socket(std::make_unique<QTcpSocket>())
{
}

TcpControlChannel::~TcpControlChannel()
{
    disconnect();
}

bool TcpControlChannel::connect(const char* host, uint16_t port)
{
    if (!m_socket) {
        return false;
    }

    QHostAddress addr(QString::fromUtf8(host));
    m_socket->connectToHost(addr, port);

    // 等待连接（阻塞式，最多 5 秒）
    if (m_socket->waitForConnected(5000)) {
        // 禁用 Nagle 算法，减少小包延迟
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        // 缩小发送缓冲区到 16KB，减少内核排队延迟
        m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 16 * 1024);
        m_connected = true;
        return true;
    }

    return false;
}

void TcpControlChannel::disconnect()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->close();
    }
    m_connected = false;
}

bool TcpControlChannel::isConnected() const
{
    return m_connected && m_socket &&
           m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpControlChannel::send(const uint8_t* data, int32_t size)
{
    if (!m_socket || !isConnected()) {
        return false;
    }

    qint64 written = m_socket->write(reinterpret_cast<const char*>(data), size);
    return written == size;
}

bool TcpControlChannel::sendTouch(uint32_t seqId, uint8_t action, uint16_t x, uint16_t y)
{
    if (!isConnected()) {
        return false;
    }

    // 使用 FastMsg 协议构建触摸消息
    FastTouchEvent event(seqId, action, x, y);
    QByteArray data = FastMsg::serializeTouch(event);

    return send(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
}

bool TcpControlChannel::sendKey(uint8_t action, int32_t keycode)
{
    if (!isConnected()) {
        return false;
    }

    // 使用 FastMsg 协议构建按键消息
    FastKeyEvent event(action, static_cast<quint16>(keycode));
    QByteArray data = FastMsg::serializeKey(event);

    return send(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
}

} // namespace core
} // namespace qsc
