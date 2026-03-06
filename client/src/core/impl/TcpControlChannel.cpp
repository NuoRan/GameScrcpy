#include "TcpControlChannel.h"
#include "NativeTcpSocket.h"
#include "fastmsg.h"

namespace qsc {
namespace core {

TcpControlChannel::TcpControlChannel()
    : m_socket(std::make_unique<NativeTcpSocket>())
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

    if (m_socket->connectToHost(std::string(host), port, 5000)) {
        m_socket->setNoDelay(true);
        m_socket->setSendBufferSize(16 * 1024);
        m_connected = true;
        return true;
    }

    return false;
}

void TcpControlChannel::disconnect()
{
    if (m_socket) {
        m_socket->close();
    }
    m_connected = false;
}

bool TcpControlChannel::isConnected() const
{
    return m_connected && m_socket && m_socket->isValid();
}

bool TcpControlChannel::send(const uint8_t* data, int32_t size)
{
    if (!m_socket || !isConnected()) {
        return false;
    }

    int written = m_socket->send(reinterpret_cast<const char*>(data), size);
    return written == size;
}

bool TcpControlChannel::sendTouch(uint32_t seqId, uint8_t action, uint16_t x, uint16_t y)
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

bool TcpControlChannel::sendKey(uint8_t action, int32_t keycode)
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

} // namespace core
} // namespace qsc
