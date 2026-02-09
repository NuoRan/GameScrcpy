#ifndef KCPSERVER_H
#define KCPSERVER_H

#include <QObject>
#include <QPointer>
#include <QSize>

#include "adbprocess.h"
#include "kcpvideosocket.h"
#include "kcpcontrolsocket.h"

/**
 * KcpServer - KCP模式服务器管理 / KCP Mode Server Manager
 *
 * 用于 WiFi 无线连接模式，使用 KCP/UDP 协议进行视频传输
 * Used for WiFi wireless connection, KCP/UDP protocol for video transport.
 * 特点：低延迟，适合实时投屏
 * Features: low latency, suitable for real-time screen mirroring.
 */
class KcpServer : public QObject
{
    Q_OBJECT

    enum SERVER_START_STEP
    {
        SSS_NULL,
        SSS_KILL_SERVER,    // 先杀死旧进程，避免端口占用
        SSS_PUSH,
        SSS_EXECUTE_SERVER,
        SSS_RUNNING,
    };

public:
    struct ServerParams
    {
        // necessary
        QString serial = "";              // 设备序列号 (格式: IP:PORT, 如 192.168.1.100:5555)
        QString serverLocalPath = "";     // 本地安卓server路径

        // optional
        QString serverRemotePath = "/data/local/tmp/scrcpy-server.jar";
        quint16 maxSize = 720;
        quint32 bitRate = 8000000;
        quint32 maxFps = 0;
        int captureOrientationLock = 0;
        int captureOrientation = 0;
        int stayAwake = false;
        QString serverVersion = "3.3.4";
        QString logLevel = "debug";
        QString codecOptions = "";
        QString codecName = "";
        QString crop = "";
        bool control = true;
        quint16 kcpPort = 27185;          // KCP UDP 视频端口 (控制端口 = kcpPort + 1)
        qint32 scid = -1;
    };

    explicit KcpServer(QObject *parent = nullptr);
    virtual ~KcpServer();

    bool start(KcpServer::ServerParams params);
    void stop();
    KcpServer::ServerParams getParams();

    // 获取 sockets
    KcpVideoSocket *removeKcpVideoSocket();
    KcpControlSocket *getKcpControlSocket();

signals:
    void serverStarted(bool success, const QString &deviceName = "", const QSize &size = QSize());
    void serverStoped();

protected:
    void timerEvent(QTimerEvent *event);

private:
    bool killOldServer();   // 杀死旧的 scrcpy 进程
    bool pushServer();
    bool execute();
    bool startServerByStep();

    void setupKcpSockets();
    void onWaitKcpTimer();

    void startWaitTimer();
    void stopWaitTimer();

    QString findClientIpInSameSubnet(const QString &deviceIp) const;

private slots:
    void onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);

private:
    qsc::AdbProcess m_workProcess;
    qsc::AdbProcess m_serverProcess;

    QPointer<KcpVideoSocket> m_kcpVideoSocket = Q_NULLPTR;
    QPointer<KcpControlSocket> m_kcpControlSocket = Q_NULLPTR;

    int m_waitTimer = 0;
    quint32 m_waitCount = 0;
    quint32 m_restartCount = 0;
    QString m_deviceName = "";
    QSize m_deviceSize = QSize();
    ServerParams m_params;

    SERVER_START_STEP m_serverStartStep = SSS_NULL;
};

#endif // KCPSERVER_H
