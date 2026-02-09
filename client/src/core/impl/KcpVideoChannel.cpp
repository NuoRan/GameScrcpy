#include "KcpVideoChannel.h"
#include "kcpvideosocket.h"

namespace qsc {
namespace core {

KcpVideoChannel::KcpVideoChannel(KcpVideoSocket* socket)
    : m_socket(socket)
{
}

bool KcpVideoChannel::connect(const char* host, uint16_t port)
{
    Q_UNUSED(host);
    Q_UNUSED(port);
    // KCP 连接由外部 Server 管理
    return m_socket && m_socket->isValid();
}

void KcpVideoChannel::disconnect()
{
    if (m_socket) {
        m_socket->close();
    }
}

bool KcpVideoChannel::isConnected() const
{
    return m_socket && m_socket->isValid();
}

int32_t KcpVideoChannel::recv(uint8_t* buf, int32_t size)
{
    if (!m_socket) return -1;
    return m_socket->subThreadRecvData(buf, size);
}

void KcpVideoChannel::setDataCallback(DataCallback callback)
{
    m_callback = std::move(callback);
    // KCP 模式使用阻塞式接收，不使用回调
}

} // namespace core
} // namespace qsc
