#include "TcpVideoChannel.h"
#include "videosocket.h"

namespace qsc {
namespace core {

TcpVideoChannel::TcpVideoChannel(VideoSocket* socket)
    : m_socket(socket)
{
}

bool TcpVideoChannel::connect(const char* host, uint16_t port)
{
    // TCP 连接由外部 Server 管理，这里不处理连接
    Q_UNUSED(host);
    Q_UNUSED(port);
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void TcpVideoChannel::disconnect()
{
    if (m_socket) {
        m_socket->requestStop();
    }
}

bool TcpVideoChannel::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

int32_t TcpVideoChannel::recv(uint8_t* buf, int32_t size)
{
    if (!m_socket) return -1;
    return m_socket->subThreadRecvData(buf, size);
}

void TcpVideoChannel::setDataCallback(DataCallback callback)
{
    m_callback = std::move(callback);
    // TCP 模式使用阻塞式接收，不使用回调
}

} // namespace core
} // namespace qsc
