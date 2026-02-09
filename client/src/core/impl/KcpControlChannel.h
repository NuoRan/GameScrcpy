#ifndef CORE_KCPCONTROLCHANNEL_H
#define CORE_KCPCONTROLCHANNEL_H

#include "interfaces/IControlChannel.h"
#include <memory>
#include <cstdint>

class KcpControlSocket;

namespace qsc {
namespace core {

/**
 * @brief KCP 控制通道实现 / KCP Control Channel Implementation
 *
 * 将 KcpControlSocket 适配为 IControlChannel 接口。
 * Adapts KcpControlSocket to the IControlChannel interface.
 * 用于 WiFi 模式下的控制命令传输。
 * Used for control command transport in WiFi mode.
 */
class KcpControlChannel : public IControlChannel {
public:
    KcpControlChannel();
    ~KcpControlChannel() override;

    // IControlChannel 实现
    bool connect(const char* host, uint16_t port) override;
    void disconnect() override;
    bool isConnected() const override;
    bool send(const uint8_t* data, int32_t size) override;
    bool sendTouch(uint32_t seqId, uint8_t action, uint16_t x, uint16_t y) override;
    bool sendKey(uint8_t action, int32_t keycode) override;
    const char* typeName() const override { return "KCP"; }

    // KCP 特有配置
    bool bind(uint16_t port = 0);
    uint16_t localPort() const;

    // 获取底层 Socket（用于兼容旧代码）
    KcpControlSocket* socket() const { return m_socket.get(); }

private:
    std::unique_ptr<KcpControlSocket> m_socket;
    bool m_connected = false;
};

} // namespace core
} // namespace qsc

#endif // CORE_KCPCONTROLCHANNEL_H
