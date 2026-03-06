#ifndef VIDEOSOCKET_H
#define VIDEOSOCKET_H

#include "NativeTcpSocket.h"
#include <cstdint>

/**
 * @brief TCP 视频接收 Socket / TCP Video Receive Socket
 *
 * 内部使用 NativeTcpSocket (Winsock2)，零 Qt 网络依赖。
 * 用于 USB/TCP 模式下传输视频数据。
 */
class VideoSocket
{
public:
    VideoSocket();
    ~VideoSocket();

    // 禁止拷贝
    VideoSocket(const VideoSocket&) = delete;
    VideoSocket& operator=(const VideoSocket&) = delete;

    /**
     * @brief 接管已有 SOCKET（来自 NativeTcpServer accept）
     */
    void adoptSocket(SOCKET handle);

    /**
     * @brief 连接到远程主机（forward 模式使用）
     */
    bool connectToHost(const char* host, uint16_t port, int timeoutMs = 1000);

    /**
     * @brief 阻塞读取初始握手数据（readInfo 调用）
     * @return 实际读取字节数，0=超时/失败
     */
    int readInfoData(unsigned char* buf, int bufSize, int timeoutMs = 3000);

    /**
     * @brief 子线程阻塞接收数据 / Block-receive data in sub-thread
     * @param buf 接收缓冲区
     * @param bufSize 需要接收的字节数
     * @return 实际接收的字节数，0=失败或停止
     */
    int32_t subThreadRecvData(uint8_t *buf, int32_t bufSize);

    /**
     * @brief 请求停止接收（线程安全）
     */
    void requestStop();

    bool isValid() const { return m_socket.isValid(); }

    /// 获取底层 NativeTcpSocket（用于 readInfo 等）
    NativeTcpSocket& nativeSocket() { return m_socket; }

private:
    NativeTcpSocket m_socket;
};

#endif // VIDEOSOCKET_H
