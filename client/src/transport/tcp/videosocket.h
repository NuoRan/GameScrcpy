#ifndef VIDEOSOCKET_H
#define VIDEOSOCKET_H

#include <QTcpSocket>
#include <atomic>

/**
 * @brief TCP 视频接收 Socket / TCP Video Receive Socket
 *
 * 用于 USB 模式下通过 adb forward 接收视频数据。
 * Receives video data via adb forward in USB mode.
 */
class VideoSocket : public QTcpSocket
{
    Q_OBJECT
public:
    explicit VideoSocket(QObject *parent = nullptr);
    virtual ~VideoSocket();

    /**
     * @brief 子线程阻塞接收数据 / Block-receive data in sub-thread
     * @param buf 接收缓冲区 / Receive buffer
     * @param bufSize 需要接收的字节数 / Bytes to receive
     * @return 实际接收的字节数 / Actual bytes received
     */
    qint32 subThreadRecvData(quint8 *buf, qint32 bufSize);

    /**
     * @brief 请求停止接收（线程安全）/ Request stop (thread-safe)
     * 设置停止标志，让 subThreadRecvData 返回。
     * Sets stop flag, causing subThreadRecvData to return.
     */
    void requestStop();

private:
    std::atomic<bool> m_stopRequested{false};
};

#endif // VIDEOSOCKET_H
