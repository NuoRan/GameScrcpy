#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QTcpServer>

/**
 * @brief TCP 服务器 / TCP Server
 *
 * 用于 USB 模式下接受来自 adb forward 的连接。
 * Accepts adb forward connections in USB mode.
 * 按顺序接受两个连接：先视频后控制。
 * Accepts two connections sequentially: video first then control.
 */
class TcpServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit TcpServer(QObject *parent = nullptr);
    virtual ~TcpServer();

protected:
    virtual void incomingConnection(qintptr handle);

private:
    bool m_isVideoSocket = true;
};

#endif // TCPSERVER_H
