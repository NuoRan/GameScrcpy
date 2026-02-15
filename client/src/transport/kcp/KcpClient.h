/**
 * @file KcpClient.h
 * @brief KCP 客户端统一接口 / KCP Client Unified Interface
 *
 * 提供视频接收和控制通道的简化 API。
 * Provides simplified API for video reception and control channel.
 */

#ifndef KCP_CLIENT_H
#define KCP_CLIENT_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QByteArray>
#include <QHostAddress>
#include <QThread>
#include <atomic>
#include <vector>

#include "KcpTransport.h"  // C-K05: 直接使用 KcpTransport 中定义的常量

/**
 * @brief 环形缓冲区 (避免 QByteArray::remove(0,n) 的 O(n) 内存搬移)
 *
 * 替代 QByteArray 的 append/remove 模式。
 * QByteArray::remove(0, n) 需要将剩余数据向前搬移 O(n)，
 * 在高码率视频流（10Mbps+）下每次搬移可达数百KB，耗时数毫秒。
 * 环形缓冲区的读写都是 O(1)，且预分配零 malloc。
 */
class CircularBuffer {
public:
    explicit CircularBuffer(int capacity = 4 * 1024 * 1024)
        : m_buffer(capacity), m_capacity(capacity) {}

    void reserve(int newCapacity) {
        if (newCapacity > m_capacity) {
            std::vector<char> newBuf(newCapacity);
            int avail = available();
            if (avail > 0) {
                peek(newBuf.data(), avail);
            }
            m_buffer = std::move(newBuf);
            m_capacity = newCapacity;
            m_readPos = 0;
            m_writePos = avail;
            m_size = avail;
        }
    }

    int write(const char* data, int len) {
        int space = freeSpace();
        if (len > space) len = space;
        if (len <= 0) return 0;

        int firstChunk = qMin(len, m_capacity - m_writePos);
        memcpy(m_buffer.data() + m_writePos, data, firstChunk);
        if (len > firstChunk) {
            memcpy(m_buffer.data(), data + firstChunk, len - firstChunk);
        }
        m_writePos = (m_writePos + len) % m_capacity;
        m_size += len;
        return len;
    }

    int read(char* data, int len) {
        int avail = available();
        if (len > avail) len = avail;
        if (len <= 0) return 0;

        int firstChunk = qMin(len, m_capacity - m_readPos);
        memcpy(data, m_buffer.data() + m_readPos, firstChunk);
        if (len > firstChunk) {
            memcpy(data + firstChunk, m_buffer.data(), len - firstChunk);
        }
        m_readPos = (m_readPos + len) % m_capacity;
        m_size -= len;
        return len;
    }

    int peek(char* data, int len) const {
        int avail = available();
        if (len > avail) len = avail;
        if (len <= 0) return 0;

        int firstChunk = qMin(len, m_capacity - m_readPos);
        memcpy(data, m_buffer.data() + m_readPos, firstChunk);
        if (len > firstChunk) {
            memcpy(data + firstChunk, m_buffer.data(), len - firstChunk);
        }
        return len;
    }

    void drop(int len) {
        int avail = available();
        if (len > avail) len = avail;
        m_readPos = (m_readPos + len) % m_capacity;
        m_size -= len;
    }

    int available() const { return m_size; }
    int freeSpace() const { return m_capacity - m_size; }
    int capacity() const { return m_capacity; }

    void clear() { m_readPos = 0; m_writePos = 0; m_size = 0; }

private:
    std::vector<char> m_buffer;
    int m_capacity = 0;
    int m_readPos = 0;
    int m_writePos = 0;
    int m_size = 0;
};

/**
 * @brief KCP 视频接收器 / KCP Video Receiver
 *
 * 基于重构的 KcpTransport，提供阻塞式接收接口（用于解码线程）。
 * Based on refactored KcpTransport, provides blocking receive API for decode thread.
 */
class KcpVideoClient : public QObject
{
    Q_OBJECT

public:
    // C-K05: 使用 KcpTransport 中定义的常量
    static constexpr uint32_t CONV_VIDEO = KcpTransport::CONV_VIDEO;
    static constexpr int DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024;  // 4MB

    explicit KcpVideoClient(QObject *parent = nullptr);
    ~KcpVideoClient() override;

    /**
     * @brief 根据码率配置参数
     * @param bitrateBps 码率（bps）
     */
    void configureBitrate(int bitrateBps);

    /**
     * @brief 绑定本地端口
     */
    bool bind(quint16 port = 0);

    /**
     * @brief 获取本地端口
     */
    quint16 localPort() const;

    /**
     * @brief 连接到远端
     */
    void connectTo(const QHostAddress &host, quint16 port);

    /**
     * @brief 是否活动
     */
    bool isActive() const;

    /**
     * @brief 阻塞式接收数据（用于解码线程）
     * @param buf 接收缓冲区
     * @param bufSize 期望接收的字节数
     * @param timeoutMs 超时时间（毫秒），-1表示无限等待
     * @return 实际接收的字节数，0表示超时或关闭
     */
    int recvBlocking(char *buf, int bufSize, int timeoutMs = -1);

    /**
     * @brief 非阻塞接收
     */
    QByteArray recv();

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
    void onDataReady();

private:
    int calculateWindowSize(int bitrateBps) const;

    /**
     * @brief 确保 IO 线程已启动，transport 已移到 IO 线程
     *
     * 在 bind()/connectTo() 时调用，延迟启动以确保
     * 构造阶段的配置（setVideoStreamMode/setMtu/setWindowSize）
     * 可在主线程安全完成。
     */
    void ensureIoThread();

private:
    KcpTransport *m_transport = nullptr;

    // 独立 IO 线程：视频 UDP 收发和 KCP 更新在此线程运行
    // 避免高码率视频流阻塞主线程事件循环，影响控制通道响应
    QThread *m_ioThread = nullptr;

    // 环形缓冲区
    CircularBuffer m_ringBuffer;
    mutable QMutex m_mutex;
    QWaitCondition m_dataAvailable;

    int m_maxBufferSize = DEFAULT_BUFFER_SIZE;
    std::atomic<bool> m_closed{false};
    std::atomic<uint64_t> m_totalRecv{0};
};

/**
 * @brief KCP控制通道客户端
 *
 * 特点:
 * - 双向通信
 * - 消息模式（保留消息边界）
 */
class KcpControlClient : public QObject
{
    Q_OBJECT

public:
    // C-K05: 使用 KcpTransport 中定义的常量
    static constexpr uint32_t CONV_CONTROL = KcpTransport::CONV_CONTROL;

    explicit KcpControlClient(QObject *parent = nullptr);
    ~KcpControlClient() override;

    /**
     * @brief 绑定本地端口
     */
    bool bind(quint16 port = 0);

    /**
     * @brief 获取本地端口
     */
    quint16 localPort() const;

    /**
     * @brief 连接到远端
     */
    void connectTo(const QHostAddress &host, quint16 port);

    /**
     * @brief 是否活动
     */
    bool isActive() const;

    /**
     * @brief 发送数据
     */
    int send(const QByteArray &data);
    int send(const char *data, int len);

    /**
     * @brief 阻塞式接收（用于控制线程）
     */
    int recvBlocking(char *buf, int bufSize, int timeoutMs = -1);

    /**
     * @brief 非阻塞接收
     */
    QByteArray recv();

    /**
     * @brief 关闭
     */
    void close();

signals:
    void connected();
    void disconnected();
    void dataReady();
    void errorOccurred(const QString &error);

private slots:
    void onDataReady();

private:
    KcpTransport *m_transport = nullptr;

    QByteArray m_buffer;
    mutable QMutex m_mutex;
    QWaitCondition m_dataAvailable;

    std::atomic<bool> m_closed{false};
};

#endif // KCP_CLIENT_H
