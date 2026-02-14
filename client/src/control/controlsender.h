#ifndef CONTROLSENDER_H
#define CONTROLSENDER_H

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>
#include <atomic>

class KcpControlSocket;

namespace qsc { namespace core { class IControlChannel; } }

using SendCallback = std::function<qint64(const QByteArray&)>;

/**
 * 控制消息即时发送器 / Control Message Instant Sender
 *
 * 直接发送模式，无队列缓冲，最低延迟。
 * Direct send mode, no queue buffering, minimal latency.
 * - KCP 模式：直接调用 KCP 写入 / KCP mode: direct KCP write
 * - TCP 模式：直接调用 TCP 写入 / TCP mode: direct TCP write
 *
 * [超低延迟优化] 支持零延迟事件循环合并模式:
 * 同一事件循环迭代内的多次 send() 合并为一次系统调用
 * 对孤立消息零额外延迟，对突发消息减少 syscall 开销
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

    // 设置控制通道接口
    void setControlChannel(qsc::core::IControlChannel* channel);

    // 设置发送回调函数
    void setSendCallback(SendCallback callback);

    /**
     * [超低延迟优化] 启用/禁用事件循环合并模式
     * 开启后，同一事件循环迭代内的多条消息合并为一次写入
     * @param enabled 是否启用 (默认关闭，保持直接发送)
     */
    void setCoalesceEnabled(bool enabled);

    // 发送数据（即时发送）
    bool send(const QByteArray &data);

    // 启动/停止发送器
    void start();
    void stop();

    // 获取统计信息
    quint64 droppedCount() const { return m_droppedCount; }
    quint64 sentCount() const { return m_sentCount; }
    quint64 batchCount() const { return m_batchCount; }

signals:
    void sendError(const QString &error);

private slots:
    void flushCoalesced();

private:
    qint64 doWriteKcp(const QByteArray &data);
    qint64 doWriteTcp(const QByteArray &data);

private:
    QPointer<KcpControlSocket> m_socket;
    QPointer<QTcpSocket> m_tcpSocket;
    qsc::core::IControlChannel* m_controlChannel = nullptr;
    SendCallback m_sendCallback;

    std::atomic<bool> m_running{false};

    // [超低延迟优化] 事件循环合并
    bool m_coalesceEnabled = false;
    QByteArray m_coalesceBuf;
    QTimer *m_coalesceTimer = nullptr;

    // 统计
    quint64 m_droppedCount = 0;
    quint64 m_sentCount = 0;
    quint64 m_batchCount = 0;  // 合并批次计数
};

#endif // CONTROLSENDER_H
