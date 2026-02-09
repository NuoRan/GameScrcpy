#ifndef CORE_TCPVIDEOCHANNEL_H
#define CORE_TCPVIDEOCHANNEL_H

#include "interfaces/IVideoChannel.h"

class VideoSocket;

namespace qsc {
namespace core {

/**
 * @brief TCP 视频通道实现 / TCP Video Channel Implementation
 *
 * 包装 VideoSocket，实现 IVideoChannel 接口。
 * Wraps VideoSocket, implements the IVideoChannel interface.
 * 用于 USB 模式下通过 adb forward 接收视频数据。
 * Used for receiving video data via adb forward in USB mode.
 */
class TcpVideoChannel : public IVideoChannel {
public:
    /**
     * @brief 构造函数
     * @param socket 外部提供的 VideoSocket（不持有所有权）
     */
    explicit TcpVideoChannel(VideoSocket* socket);
    ~TcpVideoChannel() override = default;

    // IVideoChannel 接口实现
    bool connect(const char* host, uint16_t port) override;
    void disconnect() override;
    bool isConnected() const override;
    int32_t recv(uint8_t* buf, int32_t size) override;
    void setDataCallback(DataCallback callback) override;
    const char* typeName() const override { return "TCP"; }

    // 获取底层 socket（用于兼容旧代码）
    VideoSocket* socket() const { return m_socket; }

private:
    VideoSocket* m_socket = nullptr;
    DataCallback m_callback;
};

} // namespace core
} // namespace qsc

#endif // CORE_TCPVIDEOCHANNEL_H
