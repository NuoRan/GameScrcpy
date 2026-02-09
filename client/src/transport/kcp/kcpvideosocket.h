/**
 * @file KcpVideoSocket.h
 * @brief KCP 视频接收 Socket / KCP Video Receive Socket
 *
 * 适配器，将 KcpVideoClient 适配为类似 QTcpSocket 的接口，
 * 使上层代码可以透明地切换 KCP/TCP 模式。
 * Adapter: wraps KcpVideoClient with a QTcpSocket-like interface,
 * allowing upper-layer code to transparently switch between KCP/TCP modes.
 */

#ifndef KCPVIDEOSOCKET_H
#define KCPVIDEOSOCKET_H

#include <QObject>
#include <QHostAddress>
#include <QString>
#include <atomic>

// 前向声明
class KcpVideoClient;

/**
 * @brief KCP 视频 Socket - 兼容旧接口的封装 / KCP Video Socket - Legacy-Compatible Wrapper
 *
 * 内部使用重构的 KcpVideoClient。
 * Internally uses the refactored KcpVideoClient.
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
     * @brief 根据码率配置参数
     */
    void setBitrate(quint32 bitrateBps);

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
    KcpVideoClient *m_client = nullptr;
};

#endif // KCPVIDEOSOCKET_H
