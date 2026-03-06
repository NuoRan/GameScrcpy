#include "KcpControlChannel.h"
#include "kcpcontrolsocket.h"
#include "fastmsg.h"

namespace qsc {
namespace core {

KcpControlChannel::KcpControlChannel()
    : m_socket(std::make_unique<KcpControlSocket>())
{
}

KcpControlChannel::~KcpControlChannel()
{
    disconnect();
}

bool KcpControlChannel::connect(const char* host, uint16_t port)
{
    if (!m_socket) {
        return false;
    }

    m_socket->connectToHost(std::string(host), port);

    // KCP 是 UDP，不需要等待连接
    m_connected = m_socket->isValid();
    return m_connected;
}

void KcpControlChannel::disconnect()
{
    if (m_socket) {
        m_socket->close();
    }
    m_connected = false;
}

bool KcpControlChannel::isConnected() const
{
    return m_connected && m_socket && m_socket->isValid();
}

bool KcpControlChannel::send(const uint8_t* data, int32_t size)
{
    if (!m_socket || !isConnected()) {
        return false;
    }

    int64_t written = m_socket->write(reinterpret_cast<const char*>(data), size);
    return written == size;
}

bool KcpControlChannel::sendTouch(uint32_t seqId, uint8_t action, uint16_t x, uint16_t y)
{
    if (!isConnected()) {
        return false;
    }

    // 使用 FastMsg 协议构建触摸消息
    FastTouchEvent event(seqId, action, x, y);
    char buf[6];
    int len = FastMsg::serializeTouchInto(buf, event);

    return send(reinterpret_cast<const uint8_t*>(buf), len);
}

bool KcpControlChannel::sendKey(uint8_t action, int32_t keycode)
{
    if (!isConnected()) {
        return false;
    }

    // 使用 FastMsg 协议构建按键消息
    FastKeyEvent event(action, static_cast<uint16_t>(keycode));
    char buf[3];
    FastMsg::serializeKeyInto(buf, event);

    return send(reinterpret_cast<const uint8_t*>(buf), 3);
}

bool KcpControlChannel::bind(uint16_t port)
{
    return m_socket ? m_socket->bind(port) : false;
}

uint16_t KcpControlChannel::localPort() const
{
    return m_socket ? m_socket->localPort() : 0;
}

} // namespace core
} // namespace qsc
