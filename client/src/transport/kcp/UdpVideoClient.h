/**
 * @file UdpVideoClient.h
 * @brief 裸 UDP 视频接收器 / Raw UDP Video Receiver
 *
 * 替代 KcpVideoClient 用于视频通道。
 * 无 KCP 协议栈：无 ACK、无重传、无拥塞控制、无锁开销。
 *
 * 协议: 每个 UDP 包 = [uint32 seq] + [uint8 flags] + [payload]
 *   - flags bit 0 (SOF): 帧首包标志
 *   - flags bit 1 (EOF): 帧尾包标志
 *   - 单包帧: flags = SOF|EOF (0x03)
 *
 * 帧完整性保证：
 *   客户端按 SOF→EOF 重组帧数据。若 SOF 到 EOF 之间有任何
 *   丢包（seq 不连续），整帧丢弃，不送解码器。解码器只收到完整的
 *   帧数据，即使 WiFi 丢包也不会导致字节流错位→画面脏污。
 */

#ifndef UDP_VIDEO_CLIENT_H
#define UDP_VIDEO_CLIENT_H

#include <QObject>
#include <QUdpSocket>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QHostAddress>
#include <atomic>

#include "KcpClient.h"  // CircularBuffer

/**
 * @brief 裸 UDP 视频接收器（帧级重组）
 *
 * 线程模型：
 * - IO 线程：QUdpSocket readyRead → 解析 seq/flags → 帧重组 → 完整帧写入环形缓冲区
 * - 解码线程：recvBlocking() 阻塞读取（QWaitCondition）
 *
 * 与 KcpVideoClient 接口完全兼容，KcpVideoSocket 可无缝切换。
 */
class UdpVideoClient : public QObject
{
    Q_OBJECT

public:
    // 协议常量
    static constexpr int SEQ_HEADER_SIZE = 5;                      // uint32 seq + uint8 flags

    // 帧边界标志
    static constexpr uint8_t FLAG_SOF = 0x01;   // Start of Frame
    static constexpr uint8_t FLAG_EOF = 0x02;   // End of Frame

    // 缓冲区下限（低码率时的保底值）
    static constexpr int MIN_RING_BUFFER   = 4 * 1024 * 1024;   // 4MB
    static constexpr int MIN_RECV_BUFFER   = 2 * 1024 * 1024;   // 2MB
    static constexpr int MIN_FRAME_BUFFER  = 1024 * 1024;         // 1MB

    explicit UdpVideoClient(QObject *parent = nullptr);
    ~UdpVideoClient() override;

    /**
     * @brief 根据码率和帧率配置缓冲区大小
     *
     * 必须在 bind() 之前调用。
     * 公式：
     *   环形缓冲区 = max(bitrate/8 * 3秒, 4MB)   → 缓冲约 3 秒
     *   OS接收缓冲 = max(bitrate/8/fps * 10帧, 2MB)
     *   帧重组缓冲 = max(bitrate/8/fps * 3, 256KB)
     */
    void configure(quint32 bitrateBps, quint32 maxFps);

    /**
     * @brief 绑定本地端口（服务端将向此端口发送 UDP）
     */
    bool bind(quint16 port);

    /**
     * @brief 获取本地端口
     */
    quint16 localPort() const;

    /**
     * @brief 兼容接口 — UDP 视频是单向接收，不需要 connect
     *
     * 保留此方法使 KcpVideoSocket 可以透明切换。
     */
    void connectTo(const QHostAddress &host, quint16 port);

    /**
     * @brief 是否活动
     */
    bool isActive() const;

    /**
     * @brief 阻塞式接收数据（用于解码线程）
     *
     * @param buf       接收缓冲区
     * @param bufSize   期望接收的字节数
     * @param timeoutMs 超时时间（毫秒），-1 表示无限等待
     * @return 实际接收的字节数，0 表示超时或关闭
     */
    int recvBlocking(char *buf, int bufSize, int timeoutMs = -1);

    /**
     * @brief 可用字节数
     */
    int available() const;

    /**
     * @brief 关闭
     */
    void close();

    /**
     * @brief 获取统计信息
     */
    QString stats() const;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);

private slots:
    void onSocketReadyRead();

private:
    void ensureIoThread();
    void commitFrame();

private:
    QUdpSocket *m_socket = nullptr;

    // 独立 IO 线程：UDP 收包在此线程运行
    QThread *m_ioThread = nullptr;

    // 环形缓冲区（复用 KcpClient.h 中的 CircularBuffer）
    CircularBuffer m_ringBuffer;
    mutable QMutex m_mutex;
    QWaitCondition m_dataAvailable;

    // 配置参数（由 configure() 计算）
    int m_ringBufferSize = MIN_RING_BUFFER;
    int m_recvBufferSize = MIN_RECV_BUFFER;
    int m_frameBufferSize = MIN_FRAME_BUFFER;

    // 状态
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_closed{false};

    // 序号追踪
    uint32_t m_expectedSeq = 0;
    bool m_firstPacket = true;

    // 帧重组状态机（仅 IO 线程访问，无需互斥）
    enum class FrameState { WAITING_SOF, COLLECTING };
    FrameState m_frameState = FrameState::WAITING_SOF;
    char *m_frameBuffer = nullptr;    // 帧重组缓冲区
    int m_frameLen = 0;               // 当前已累积字节数
    uint32_t m_lastSeq = 0;           // 当前帧的上一个 seq

    // 统计
    std::atomic<uint64_t> m_totalRecv{0};
    std::atomic<uint64_t> m_totalPackets{0};
    std::atomic<uint64_t> m_gapCount{0};
    std::atomic<uint64_t> m_droppedFrames{0};
    std::atomic<uint64_t> m_completedFrames{0};
};

#endif // UDP_VIDEO_CLIENT_H
