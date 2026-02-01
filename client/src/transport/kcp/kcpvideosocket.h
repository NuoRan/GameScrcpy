/**
 * @file kcpvideosocket.h
 * @brief KCP视频接收Socket - 适配器模式
 *
 * 设计说明 (C-S01):
 * 此类作为适配器，将 KcpVideoClient 适配为类似 QTcpSocket 的接口
 * 保留此封装是为了与 VideoSocket (TCP模式) 保持统一的使用方式
 * 这样 Server/Demuxer 等上层代码可以透明地切换 KCP/TCP 模式
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
 * @brief KCP视频Socket - 兼容旧接口的封装
 *
 * 内部使用全新重构的 KcpVideoClient
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
