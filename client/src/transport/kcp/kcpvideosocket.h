/**
 * @file KcpVideoSocket.h
 * @brief UDP 视频接收 Socket / UDP Video Receive Socket
 *
 * 适配器，将 UdpVideoClient 适配为类似 QTcpSocket 的接口，
 * 使上层代码可以透明地切换 UDP/TCP 模式。
 * Adapter: wraps UdpVideoClient with a QTcpSocket-like interface,
 * allowing upper-layer code to transparently switch between UDP/TCP modes.
 */

#ifndef KCPVIDEOSOCKET_H
#define KCPVIDEOSOCKET_H

#include <QObject>
#include <QHostAddress>
#include <QString>
#include <atomic>

// 前向声明
class UdpVideoClient;

/**
 * @brief UDP 视频 Socket - 兼容旧接口的封装 / UDP Video Socket - Legacy-Compatible Wrapper
 *
 * 内部使用 UdpVideoClient（裸 UDP，无 KCP 开销）。
 * Internally uses UdpVideoClient (raw UDP, no KCP overhead).
 */
class KcpVideoSocket : public QObject
{
    Q_OBJECT

public:
    static constexpr quint32 KCP_CONV = 0x11223344;
    static constexpr int UPDATE_INTERVAL_MS = 10;

    explicit KcpVideoSocket(QObject *parent = nullptr);
    virtual ~KcpVideoSocket();

    /**
     * @brief 根据码率和帧率配置缓冲区
     */
    void setBitrate(quint32 bitrateBps, quint32 maxFps = 60);

    /**
     * @brief 绑定本地端口
     */
    bool bind(quint16 port = 0);

    /**
     * @brief 获取本地端口
     */
    quint16 localPort() const;

    /**
     * @brief 获取本地地址
     */
    QHostAddress localAddress() const;

    /**
     * @brief 连接到远端
     */
    void connectToHost(const QHostAddress &host, quint16 port);

    /**
     * @brief 是否有效
     */
    bool isValid() const;

    /**
     * @brief 子线程接收数据（阻塞式）
     */
    qint32 subThreadRecvData(quint8 *buf, qint32 bufSize);

    /**
     * @brief 关闭
     */
    void close();

    /**
     * @brief 可用字节数
     */
    qint64 bytesAvailable() const;

    /**
     * @brief 获取统计信息
     */
    QString getStats() const;

signals:
    void readyRead();
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);

private:
    UdpVideoClient *m_client = nullptr;
};

#endif // KCPVIDEOSOCKET_H
