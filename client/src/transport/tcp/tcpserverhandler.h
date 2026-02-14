#ifndef TCPSERVERHANDLER_H
#define TCPSERVERHANDLER_H

#include <QObject>
#include <QPointer>
#include <QSize>

#include "adbprocess.h"
#include "tcpserver.h"
#include "videosocket.h"

/**
 * TcpServerHandler - TCP模式服务器管理 / TCP Mode Server Manager
 *
 * 用于 USB 有线连接模式，使用 TCP 协议通过 adb forward/reverse 进行视频传输
 * Used for USB wired connection, TCP protocol via adb forward/reverse for video transport.
 * 特点：稳定可靠，兼容性好 / Features: stable, reliable, good compatibility.
 *
 * 支持两种隧道模式 / Supports two tunnel modes:
 * - adb reverse (默认/default): 服务端连接到客户端 / server connects to client
 * - adb forward: 客户端连接到服务端 / client connects to server
 */
class TcpServerHandler : public QObject
{
    Q_OBJECT

    enum SERVER_START_STEP
    {
        SSS_NULL,
        SSS_PUSH,
        SSS_ENABLE_TUNNEL_REVERSE,
        SSS_ENABLE_TUNNEL_REVERSE_CTRL,
        SSS_ENABLE_TUNNEL_FORWARD,
        SSS_ENABLE_TUNNEL_FORWARD_CTRL,
        SSS_EXECUTE_SERVER,
        SSS_RUNNING,
    };

public:
    struct ServerParams
    {
        // necessary
        QString serial = "";              // 设备序列号 (如 abcd1234)
        QString serverLocalPath = "";     // 本地安卓server路径

        // optional
        QString serverRemotePath = "/data/local/tmp/scrcpy-server.jar";
        quint16 localPort = 27183;        // reverse时本地监听端口
        quint16 localPortCtrl = 27184;    // 控制socket端口
        quint16 maxSize = 720;
        quint32 bitRate = 8000000;
        quint32 maxFps = 0;
        bool useReverse = true;           // true: 先使用 adb reverse，失败后自动使用 adb forward
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
        qint32 scid = -1;
    };

    explicit TcpServerHandler(QObject *parent = nullptr);
    virtual ~TcpServerHandler();

    bool start(TcpServerHandler::ServerParams params);
    void stop();
    bool isReverse();
    TcpServerHandler::ServerParams getParams();
    VideoSocket *removeVideoSocket();
    QTcpSocket *getControlSocket();

signals:
    void serverStarted(bool success, const QString &deviceName = "", const QSize &size = QSize());
    void serverStoped();

private slots:
    void onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);

protected:
    void timerEvent(QTimerEvent *event);

private:
    bool pushServer();
    bool enableTunnelReverse();
    bool enableTunnelReverseCtrl();
    bool disableTunnelReverse();
    bool enableTunnelForward();
    bool enableTunnelForwardCtrl();
    bool disableTunnelForward();
    bool execute();
    bool connectTo();
    bool startServerByStep();
    bool readInfo(VideoSocket *videoSocket, QString &deviceName, QSize &size);
    void startAcceptTimeoutTimer();
    void stopAcceptTimeoutTimer();
    void startConnectTimeoutTimer();
    void stopConnectTimeoutTimer();
    void onConnectTimer();
    void checkBothConnected();

private:
    qsc::AdbProcess m_workProcess;
    qsc::AdbProcess m_serverProcess;
    TcpServer m_serverSocket;        // video socket server (only used if !tunnel_forward)
    TcpServer m_serverSocketCtrl;    // control socket server (only used if !tunnel_forward)
    QPointer<VideoSocket> m_videoSocket = Q_NULLPTR;
    QPointer<QTcpSocket> m_controlSocket = Q_NULLPTR;
    bool m_tunnelEnabled = false;
    bool m_tunnelForward = false;    // use "adb forward" instead of "adb reverse"
    int m_acceptTimeoutTimer = 0;
    int m_connectTimeoutTimer = 0;
    quint32 m_connectCount = 0;
    quint32 m_restartCount = 0;
    QString m_deviceName = "";
    QSize m_deviceSize = QSize();
    ServerParams m_params;

    SERVER_START_STEP m_serverStartStep = SSS_NULL;
};

#endif // TCPSERVERHANDLER_H
