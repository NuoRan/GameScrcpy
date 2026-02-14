#ifndef VIDEOSOCKET_H
#define VIDEOSOCKET_H

#include <QTcpSocket>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>

/**
 * @brief TCP 视频接收 Socket / TCP Video Receive Socket
 *
 * 用于 USB 模式下通过 adb forward 接收视频数据。
 * Receives video data via adb forward in USB mode.
 *
 * [超低延迟优化] 使用 QWaitCondition 事件驱动替代 10ms 轮询，
 * 当数据到达时立即唤醒等待线程，消除平均 5ms 的轮询延迟。
 * Uses QWaitCondition event-driven approach instead of 10ms polling,
 * wakes the waiting thread immediately when data arrives.
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

private slots:
    /**
     * @brief 数据到达通知 / Data arrival notification
     * 由 readyRead 信号触发，唤醒等待中的子线程。
     */
    void onReadyRead();

private:
    std::atomic<bool> m_stopRequested{false};

    // [超低延迟] 事件驱动同步原语
    QMutex m_waitMutex;
    QWaitCondition m_dataAvailable;
};

#endif // VIDEOSOCKET_H
