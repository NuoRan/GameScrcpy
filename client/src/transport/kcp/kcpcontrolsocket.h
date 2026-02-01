/**
 * @file kcpcontrolsocket.h
 * @brief KCP控制Socket - 适配器模式
 *
 * 设计说明 (C-S02):
 * 此类作为适配器，将 KcpControlClient 适配为类似 QTcpSocket 的接口
 * 保留此封装是为了与 QTcpSocket (TCP模式) 保持统一的使用方式
 * 这样 Controller/ControlSender 等上层代码可以透明地切换 KCP/TCP 模式
 */

#ifndef KCPCONTROLSOCKET_H
#define KCPCONTROLSOCKET_H

#include <QObject>
#include <QHostAddress>
#include <QByteArray>
#include <atomic>

// 前向声明
class KcpControlClient;

/**
 * @brief KCP控制Socket - 兼容旧接口的封装
 *
 * 内部使用全新重构的 KcpControlClient
 */
class KcpControlSocket : public QObject
{
    Q_OBJECT

public:
    static constexpr quint32 KCP_CONV_CONTROL = 0x22334455;
    static constexpr int UPDATE_INTERVAL_MS = 10;
    static constexpr int MAX_RECV_BUFFER = 64 * 1024;

    explicit KcpControlSocket(QObject *parent = nullptr);
    virtual ~KcpControlSocket();

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
     * @brief 发送数据
     */
    qint64 write(const char *data, qint64 len);
    qint64 write(const QByteArray &data);

    /**
     * @brief 读取所有可用数据
     */
    QByteArray readAll();

    /**
     * @brief 可用字节数
     */
    qint64 bytesAvailable() const;

    /**
     * @brief 关闭
     */
    void close();

signals:
    void readyRead();
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);

private slots:
    void onDataReady();

private:
    KcpControlClient *m_client = nullptr;
    QByteArray m_readBuffer;
};

#endif // KCPCONTROLSOCKET_H
