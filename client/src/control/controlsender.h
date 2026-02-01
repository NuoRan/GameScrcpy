#ifndef CONTROLSENDER_H
#define CONTROLSENDER_H

#include <QObject>
#include <QQueue>
#include <QByteArray>
#include <QPointer>
#include <QTcpSocket>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <QTimer>
#include <functional>
#include <atomic>

class KcpControlSocket;

// 发送回调函数类型
using SendCallback = std::function<qint64(const QByteArray&)>;

/**
 * 控制消息异步发送器
 *
 * A-06 优化: 使用条件变量替代 QTimer 轮询
 *
 * 原方案: QTimer 每 2ms 触发一次检查队列 (高 CPU, 固定延迟)
 * 新方案:
 * - KCP 模式: QWaitCondition 事件驱动 (低 CPU, 即时响应)
 * - TCP 模式: QTimer 主线程发送 (QTcpSocket 不支持跨线程)
 *
 * 支持两种模式:
 * - KCP 模式 (WiFi): setSocket(KcpControlSocket*) - 使用工作线程
 * - TCP 模式 (USB): setTcpSocket(QTcpSocket*) - 使用主线程定时器
 */
class ControlSender : public QObject
{
    Q_OBJECT

public:
    explicit ControlSender(QObject *parent = nullptr);
    ~ControlSender();

    // 设置目标 KCP socket (WiFi 模式)
    void setSocket(KcpControlSocket *socket);

    // 设置目标 TCP socket (USB 模式)
    void setTcpSocket(QTcpSocket *socket);

    // 设置发送回调函数
    void setSendCallback(SendCallback callback);

    // 发送数据（非阻塞，可能丢弃）
    bool send(const QByteArray &data);

    // 启动/停止发送器
    void start();
    void stop();

    // 获取统计信息
    quint64 droppedCount() const { return m_droppedCount; }
    quint64 sentCount() const { return m_sentCount; }
    int pendingBytes() const { return m_pendingBytes; }

signals:
    void sendError(const QString &error);
    void bufferWarning(int pendingBytes, int threshold);

private slots:
    void onTcpFlushTimer();                        // TCP 模式主线程定时器回调

private:
    void workerLoop();                             // A-06: KCP 模式工作线程循环
    qint64 doWriteKcp(const QByteArray &data);     // KCP 写入接口 (线程安全)
    qint64 doWriteTcp(const QByteArray &data);     // TCP 写入接口 (仅主线程)
    void enqueue(const QByteArray &data);          // 带累计的入队
    QByteArray dequeue();                          // 带累计的出队
    void prepend(const QByteArray &data);          // 带累计的前置入队
    void processQueue();                           // 处理队列中的数据

private:
    QPointer<KcpControlSocket> m_socket;
    QPointer<QTcpSocket> m_tcpSocket;
    SendCallback m_sendCallback;
    QQueue<QByteArray> m_queue;

    // A-06: 条件变量相关成员 (仅 KCP 模式使用)
    QThread *m_workerThread = nullptr;
    QMutex m_mutex;
    QWaitCondition m_condition;
    std::atomic<bool> m_running{false};

    // TCP 模式: 使用主线程定时器 (QTcpSocket 不能跨线程)
    QTimer *m_tcpFlushTimer = nullptr;

    // 阈值配置（控制消息很小，用较小缓冲区避免延迟）
    static const int MAX_PENDING_BYTES = 2 * 1024;      // 2KB 最大积压
    static const int WARN_PENDING_BYTES = 1 * 1024;     // 1KB 警告阈值
    static const int MAX_QUEUE_SIZE = 64;               // 最大队列长度

    // 统计
    quint64 m_droppedCount = 0;
    quint64 m_sentCount = 0;
    int m_pendingBytes = 0;
};

#endif // CONTROLSENDER_H
