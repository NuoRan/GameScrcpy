/**
 * @file KcpTransport.h
 * @brief KCP 传输层 / KCP Transport Layer
 *
 * 封装 UDP 收发和 KCP 定时更新，提供简洁的 Qt 风格 API。
 * Encapsulates UDP send/recv and KCP timer update with a clean Qt-style API.
 *
 * 使用示例:
 * @code
 * KcpTransport transport(0x11223344);
 *
 * // 绑定端口（服务端）或连接远端（客户端）
 * transport.bind(27185);
 * // 或
 * transport.connectTo("192.168.1.100", 27185);
 *
 * // 发送数据
 * transport.send(data, len);
 *
 * // 接收数据（信号驱动）
 * connect(&transport, &KcpTransport::dataReady, [&]() {
 *     QByteArray data = transport.recv();
 *     processData(data);
 * });
 * @endcode
 */

#ifndef KCP_TRANSPORT_H
#define KCP_TRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QByteArray>
#include <memory>

#include "KcpCore.h"

/**
 * @brief KCP 传输层 - UDP + KCP 协议栈 / KCP Transport - UDP + KCP Protocol Stack
 *
 * 职责 / Responsibilities:
 * - 管理 UDP 套接字 / Manage UDP socket
 * - 定时调用 KCP update / Periodically call KCP update
 * - 转发数据收发 / Forward data send/recv
 */
class KcpTransport : public QObject
{
    Q_OBJECT

public:
    // 预定义会话ID
    static constexpr uint32_t CONV_VIDEO = 0x11223344;
    static constexpr uint32_t CONV_CONTROL = 0x22334455;

    /**
     * @brief 构造函数
     * @param conv 会话ID，通信双方必须相同
     * @param parent 父对象
     */
    explicit KcpTransport(uint32_t conv, QObject *parent = nullptr);

    ~KcpTransport() override;

    // 禁止拷贝
    KcpTransport(const KcpTransport &) = delete;
    KcpTransport &operator=(const KcpTransport &) = delete;

    //=========================================================================
    // 连接管理
    //=========================================================================

    /**
     * @brief 绑定本地端口（服务端模式）
     * @param port 本地端口，0表示随机
     * @return 成功返回true
     */
    bool bind(quint16 port = 0);

    /**
     * @brief 连接到远端（客户端模式）
     * @param address 远端地址
     * @param port 远端端口
     */
    void connectTo(const QHostAddress &address, quint16 port);

    /**
     * @brief 断开连接
     */
    void close();

    /**
     * @brief 是否已连接/绑定
     */
    bool isActive() const { return m_active; }

    /**
     * @brief 获取本地端口
     */
    quint16 localPort() const;

    /**
     * @brief 获取远端地址
     */
    QHostAddress remoteAddress() const { return m_remoteAddress; }

    /**
     * @brief 获取远端端口
     */
    quint16 remotePort() const { return m_remotePort; }

    //=========================================================================
    // 数据传输
    //=========================================================================

    /**
     * @brief 发送数据（可靠传输）
     * @return 0成功，<0失败
     */
    int send(const char *data, int len);
    int send(const QByteArray &data);

    /**
     * @brief 接收数据
     * @return 接收到的数据，无数据返回空
     */
    QByteArray recv();

    /**
     * @brief 查看下一个消息大小
     */
    int peekSize() const;

    /**
     * @brief 待发送队列大小
     */
    int pending() const;

    //=========================================================================
    // 配置 (参考 test.cpp 的三种模式)
    //=========================================================================

    /**
     * @brief 快速模式 - 极致低延迟（游戏/投屏推荐）
     *
     * - nodelay=2, interval=1, resend=2, nc=1
     * - rx_minrto=1, fastresend=1
     * - window=256x256
     */
    void setFastMode();

    /**
     * @brief 视频流模式 - 高带宽优化
     *
     * 在快速模式基础上启用流模式和更大窗口
     */
    void setVideoStreamMode();

    /**
     * @brief 普通模式 - 关闭流控
     *
     * 参考 test.cpp mode=1:
     * - nodelay=0, interval=10, resend=0, nc=1
     */
    void setNormalMode();

    /**
     * @brief 默认模式 - 类似TCP
     *
     * 参考 test.cpp mode=0:
     * - nodelay=0, interval=10, resend=0, nc=0
     */
    void setDefaultMode();

    /**
     * @brief 设置窗口大小
     */
    void setWindowSize(int sndwnd, int rcvwnd);

    /**
     * @brief 设置MTU
     */
    void setMtu(int mtu);

    /**
     * @brief 设置更新间隔
     * @param interval 毫秒，默认10
     */
    void setUpdateInterval(int interval);

    //=========================================================================
    // 高级配置
    //=========================================================================

    /**
     * @brief 设置nodelay参数
     */
    void setNoDelay(int nodelay, int interval, int resend, int nc);

    /**
     * @brief 设置最小RTO
     */
    void setMinRto(int minrto);

    /**
     * @brief 设置流模式
     * @param stream 0:消息模式(保持消息边界) 1:流模式(合并数据)
     */
    void setStreamMode(int stream);

    /**
     * @brief 获取KCP核心对象（高级用法）
     */
    KcpCore *core() { return m_kcp.get(); }

signals:
    /**
     * @brief 有数据可读
     */
    void dataReady();

    /**
     * @brief 远端已连接（收到第一个有效数据包）
     */
    void peerConnected();

    /**
     * @brief 连接断开
     */
    void disconnected();

    /**
     * @brief 发生错误
     */
    void errorOccurred(const QString &error);

private slots:
    void onSocketReadyRead();
    void onUpdateTimer();

private:
    // UDP输出回调
    int udpOutput(const char *buf, int len);

    // 获取当前毫秒时间戳
    uint32_t currentMs() const;

    // 【按需调度优化】计算并设置下次更新时间
    void scheduleNextUpdate();

private:
    std::unique_ptr<KcpCore> m_kcp;

    QUdpSocket *m_socket = nullptr;
    QTimer *m_updateTimer = nullptr;
    QElapsedTimer m_clock;

    QHostAddress m_remoteAddress;
    quint16 m_remotePort = 0;

    bool m_active = false;
    int m_updateInterval = 1;  // 每1ms调用一次update，保证低延迟
};

#endif // KCP_TRANSPORT_H
