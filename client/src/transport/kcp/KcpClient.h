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
#include <atomic>

#include "KcpTransport.h"  // C-K05: 直接使用 KcpTransport 中定义的常量

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

private:
    KcpTransport *m_transport = nullptr;

    QByteArray m_buffer;
    int m_readOffset = 0;
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
