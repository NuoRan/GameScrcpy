#ifndef AUXCHANNELCLIENT_H
#define AUXCHANNELCLIENT_H

#include <cstdint>

class NativeTcpSocket;

/**
 * @brief 辅助通道客户端 / Auxiliary Channel Client
 *
 * 独立于控制通道的第三条通信通道 (TCP only)。
 * Independent third communication channel, separate from control channel.
 *
 * 用途 / Purpose:
 * - 视频参数实时调整 (bitrate / fps / maxSize)
 * - 视频流暂停/恢复
 * - 未来扩展: 音频、剪切板等 / Future: audio, clipboard, etc.
 *
 * 消息格式 / Message format: [type:1B][payload]
 * 与服务端 AuxMessageReader 对应。
 */
class AuxChannelClient
{
public:
    // 消息类型 (与服务端 AuxMessage.java 对应)
    enum AuxMsgType : uint8_t {
        TYPE_SET_VIDEO_PARAMS    = 0x01,  // bitrate(4)+maxFps(2)+maxSize(2) = 9B total
        TYPE_SET_VIDEO_STREAMING = 0x02,  // mode(1) = 2B total
    };

    AuxChannelClient() = default;
    ~AuxChannelClient() = default;

    /**
     * @brief 设置 TCP socket
     */
    void setTcpSocket(NativeTcpSocket *socket);

    /**
     * @brief 发送视频参数变更
     * @param bitrate  目标码率
     * @param maxFps   目标帧率 (0=不限, 0xFFFF=不变)
     * @param maxSize  目标分辨率 (0=原始, 0xFFFF=不变)
     */
    void sendVideoParams(uint32_t bitrate, uint16_t maxFps, uint16_t maxSize);

    /**
     * @brief 发送视频流暂停/恢复
     * @param on  true=恢复, false=暂停
     */
    void sendVideoStreaming(bool on);

    /**
     * @brief 是否已配置可用
     */
    bool isReady() const;

private:
    void writeMessage(const char *data, int size);

    NativeTcpSocket *m_tcpSocket = nullptr;
};

#endif // AUXCHANNELCLIENT_H
