#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTcpSocket>

// 前向声明
class KcpServer;
class TcpServerHandler;
class KcpVideoSocket;
class KcpControlSocket;
class VideoSocket;

/**
 * @brief 统一的服务器管理接口 / Unified Server Management Interface
 *
 * 自动根据设备 serial 格式选择连接模式 / Auto-selects connection mode by serial format:
 * - 包含 ':' (如 192.168.1.100:5555): WiFi 模式 (KCP)
 *   Contains ':': WiFi mode (KCP)
 * - 不包含 ':' (如 abcd1234): USB 模式 (TCP)
 *   No ':': USB mode (TCP)
 */
class Server : public QObject
{
    Q_OBJECT

public:
    struct ServerParams
    {
        // 必需参数 / Required parameters
        QString serial = "";              // 设备序列号 / Device serial
        QString serverLocalPath = "";     // 本地 server 路径 / Local server path

        // 可选参数 / Optional parameters
        QString serverRemotePath = "/data/local/tmp/scrcpy-server.jar";
        quint16 maxSize = 720;
        quint32 bitRate = 8000000;
        quint32 maxFps = 0;
        int captureOrientationLock = 0;
        int captureOrientation = 0;
        int stayAwake = false;
        QString serverVersion = "3.3.4";
        QString logLevel = "debug";
        QString videoCodec = "h264";  // "h264"
        QString codecOptions = "";
        QString codecName = "";
        QString crop = "";
        bool control = true;

        // TCP 模式参数 / TCP mode parameters
        quint16 localPort = 27183;        // TCP 本地端口 (USB 模式) / TCP local port (USB)
        quint16 localPortCtrl = 27184;    // TCP 控制端口 / TCP control port
        bool useReverse = true;           // TCP 模式: 先尝试 reverse / TCP: try reverse first

        // KCP 模式参数 / KCP mode parameters
        quint16 kcpPort = 27185;          // KCP UDP 视频端口 / KCP UDP video port (ctrl = kcpPort+1)

        qint32 scid = -1;
    };

    explicit Server(QObject *parent = nullptr);
    virtual ~Server();

    bool start(Server::ServerParams params);
    void stop();
    Server::ServerParams getParams();

    // 连接模式判断
    bool isWiFiMode() const { return m_useKcp; }
    bool isUsbMode() const { return !m_useKcp; }

    // TCP 模式: 是否使用 reverse
    bool isReverse() const;

    // 获取 sockets (KCP 模式)
    KcpVideoSocket *removeKcpVideoSocket();
    KcpControlSocket *getKcpControlSocket();

    // 获取 sockets (TCP 模式)
    VideoSocket *removeVideoSocket();
    QTcpSocket *getControlSocket();

signals:
    void serverStarted(bool success, const QString &deviceName = "", const QSize &size = QSize());
    void serverStoped();

private:
    bool m_useKcp = false;
    ServerParams m_params;

    // 内部实现 (互斥)
    QPointer<KcpServer> m_kcpServer;
    QPointer<TcpServerHandler> m_tcpServer;
};

#endif // SERVER_H
