#ifndef CORE_TCPCONTROLCHANNEL_H
#define CORE_TCPCONTROLCHANNEL_H

#include "interfaces/IControlChannel.h"
#include <memory>
#include <QTcpSocket>

namespace qsc {
namespace core {

/**
 * @brief TCP 控制通道实现 / TCP Control Channel Implementation
 *
 * 使用 QTcpSocket 实现 IControlChannel 接口。
 * Implements IControlChannel using QTcpSocket.
 * 用于 USB 模式下的控制命令传输。
 * Used for control command transport in USB mode.
 */
class TcpControlChannel : public IControlChannel {
public:
    TcpControlChannel();
    ~TcpControlChannel() override;

    // IControlChannel 实现
    bool connect(const char* host, uint16_t port) override;
    void disconnect() override;
    bool isConnected() const override;
    bool send(const uint8_t* data, int32_t size) override;
    bool sendTouch(uint32_t seqId, uint8_t action, uint16_t x, uint16_t y) override;
    bool sendKey(uint8_t action, int32_t keycode) override;
    const char* typeName() const override { return "TCP"; }

    // 获取底层 Socket（用于兼容旧代码）
    QTcpSocket* socket() const { return m_socket.get(); }

private:
    std::unique_ptr<QTcpSocket> m_socket;
    bool m_connected = false;
};

} // namespace core
} // namespace qsc

#endif // CORE_TCPCONTROLCHANNEL_H
