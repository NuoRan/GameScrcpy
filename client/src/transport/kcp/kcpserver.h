#ifndef KCPSERVER_H
#define KCPSERVER_H

#include "GameSignal.h"
#include "NativeTimer.h"

#include "GameTypes.h"
#include <string>
#include <cstdint>

#include "adbprocess.h"

class NativeTcpSocket;
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
class KcpServer
{
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
        std::string serial;              // 设备序列号 (格式: IP:PORT, 如 192.168.1.100:5555)
        std::string serverLocalPath;     // 本地安卓server路径

        // optional
        std::string serverRemotePath = "/data/local/tmp/scrcpy-server.jar";
        uint16_t maxSize = 720;
        uint32_t bitRate = 8000000;
        uint32_t maxFps = 0;
        int captureOrientationLock = 0;
        int captureOrientation = 0;
        int stayAwake = false;
        std::string serverVersion = "3.3.4";
        std::string logLevel = "debug";
        std::string videoCodec = "h264";  // "h264"
        std::string codecOptions;
        std::string codecName;
        std::string crop;
        bool control = true;
        bool audioEnabled = true;     // 是否启用音频通道
        bool auxEnabled = true;       // 是否启用辅助通道
        uint16_t kcpPort = 27185;          // KCP UDP 视频端口 (控制端口 = kcpPort + 1)
        int32_t scid = -1;
    };

    explicit KcpServer();
    virtual ~KcpServer();

    bool start(KcpServer::ServerParams params);
    void stop();
    KcpServer::ServerParams getParams();

    // 获取 sockets
    KcpVideoSocket *removeKcpVideoSocket();
    KcpControlSocket *getKcpControlSocket();

    // TCP 音频/辅助通道 (WiFi 模式通过 TCP 传输)
    NativeTcpSocket *getAudioTcpSocket() { return m_audioTcpSocket; }
    NativeTcpSocket *getAuxTcpSocket() { return m_auxTcpSocket; }

    // 服务器 IP
    std::string getServerIp() const {
        auto pos = m_params.serial.find(':');
        return (pos != std::string::npos) ? m_params.serial.substr(0, pos) : m_params.serial;
    }

    // 信号 (Signal<>) — 替代 Qt signals
    Signal<bool, const std::string&, const Size&> serverStarted;
    Signal<> serverStoped;

private:
    bool killOldServer();   // 杀死旧的 scrcpy 进程
    bool pushServer();
    bool execute();
    bool startServerByStep();

    void setupKcpSockets();
    void onWaitKcpTimer();

    void startWaitTimer();
    void stopWaitTimer();

    std::string findClientIpInSameSubnet(const std::string &deviceIp) const;

    void onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);
    void onServerProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);

private:
    qsc::AdbProcess m_workProcess;
    qsc::AdbProcess m_serverProcess;

    KcpVideoSocket* m_kcpVideoSocket = nullptr;
    KcpControlSocket* m_kcpControlSocket = nullptr;
    NativeTcpSocket *m_audioTcpSocket = nullptr;
    NativeTcpSocket *m_auxTcpSocket = nullptr;

    NativeTimer m_waitTimer;
    uint32_t m_waitCount = 0;
    uint32_t m_restartCount = 0;
    std::string m_deviceName;
    Size m_deviceSize;
    ServerParams m_params;

    SERVER_START_STEP m_serverStartStep = SSS_NULL;
};

#endif // KCPSERVER_H
