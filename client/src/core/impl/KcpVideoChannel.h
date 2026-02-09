#ifndef CORE_KCPVIDEOCHANNEL_H
#define CORE_KCPVIDEOCHANNEL_H

#include "interfaces/IVideoChannel.h"

class KcpVideoSocket;

namespace qsc {
namespace core {

/**
 * @brief KCP 视频通道实现 / KCP Video Channel Implementation
 *
 * 包装 KcpVideoSocket，实现 IVideoChannel 接口。
 * Wraps KcpVideoSocket, implements the IVideoChannel interface.
 * 用于 WiFi 模式下通过 KCP/UDP 接收视频数据。
 * Used for receiving video data via KCP/UDP in WiFi mode.
 */
class KcpVideoChannel : public IVideoChannel {
public:
    /**
     * @brief 构造函数
     * @param socket 外部提供的 KcpVideoSocket（不持有所有权）
     */
    explicit KcpVideoChannel(KcpVideoSocket* socket);
    ~KcpVideoChannel() override = default;

    // IVideoChannel 接口实现
    bool connect(const char* host, uint16_t port) override;
    void disconnect() override;
    bool isConnected() const override;
    int32_t recv(uint8_t* buf, int32_t size) override;
    void setDataCallback(DataCallback callback) override;
    const char* typeName() const override { return "KCP"; }

    // 获取底层 socket（用于兼容旧代码）
    KcpVideoSocket* socket() const { return m_socket; }

private:
    KcpVideoSocket* m_socket = nullptr;
    DataCallback m_callback;
};

} // namespace core
} // namespace qsc

#endif // CORE_KCPVIDEOCHANNEL_H
