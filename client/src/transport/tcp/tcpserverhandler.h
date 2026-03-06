#ifndef TCPSERVERHANDLER_H
#define TCPSERVERHANDLER_H

#include "GameSignal.h"
#include "NativeTimer.h"

#include "GameTypes.h"
#include <string>
#include <cstdint>

#include "adbprocess.h"
#include "NativeTcpServer.h"

class VideoSocket;
class NativeTcpSocket;

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
class NativeTcpSocket;

class TcpServerHandler
{
    enum SERVER_START_STEP
    {
        SSS_NULL,
        SSS_PUSH,
        SSS_ENABLE_TUNNEL_REVERSE,
        SSS_ENABLE_TUNNEL_REVERSE_AUDIO,
        SSS_ENABLE_TUNNEL_REVERSE_CTRL,
        SSS_ENABLE_TUNNEL_REVERSE_AUX,
        SSS_ENABLE_TUNNEL_FORWARD,
        SSS_ENABLE_TUNNEL_FORWARD_AUDIO,
        SSS_ENABLE_TUNNEL_FORWARD_CTRL,
        SSS_ENABLE_TUNNEL_FORWARD_AUX,
        SSS_EXECUTE_SERVER,
        SSS_RUNNING,
    };

public:
    struct ServerParams
    {
        // necessary
        std::string serial;              // 设备序列号 (如 abcd1234)
        std::string serverLocalPath;     // 本地安卓server路径

        // optional
        std::string serverRemotePath = "/data/local/tmp/scrcpy-server.jar";
        uint16_t localPort = 27183;        // reverse时本地监听端口
        uint16_t localPortAudio = 27186;    // 音频socket端口 / audio socket port
        uint16_t localPortCtrl = 27184;    // 控制socket端口
        uint16_t localPortAux = 27185;     // 辅助通道端口 / auxiliary channel port
        uint16_t maxSize = 720;
        uint32_t bitRate = 8000000;
        uint32_t maxFps = 0;
        bool useReverse = true;           // true: 先使用 adb reverse，失败后自动使用 adb forward
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
        int32_t scid = -1;
    };

    explicit TcpServerHandler();
    virtual ~TcpServerHandler();

    bool start(TcpServerHandler::ServerParams params);
    void stop();
    bool isReverse();
    TcpServerHandler::ServerParams getParams();
    VideoSocket *removeVideoSocket();
    NativeTcpSocket *getControlSocket();
    NativeTcpSocket *getAudioSocket();
    NativeTcpSocket *getAuxSocket();

    // 信号 (Signal<>) — 替代 Qt signals
    Signal<bool, const std::string&, const Size&> serverStarted;
    Signal<> serverStoped;

private:
    bool pushServer();
    bool enableTunnelReverse();
    bool enableTunnelReverseAudio();
    bool enableTunnelReverseCtrl();
    bool enableTunnelReverseAux();
    bool disableTunnelReverse();
    bool enableTunnelForward();
    bool enableTunnelForwardAudio();
    bool enableTunnelForwardCtrl();
    bool enableTunnelForwardAux();
    bool disableTunnelForward();
    bool execute();
    bool connectTo();
    bool startServerByStep();
    SERVER_START_STEP nextStepAfterReverse(SERVER_START_STEP completed);
    SERVER_START_STEP nextStepAfterForward(SERVER_START_STEP completed);
    bool listenOnEnabledPorts();
    bool readInfo(VideoSocket *videoSocket, std::string &deviceName, Size &size);
    void startAcceptTimeoutTimer();
    void stopAcceptTimeoutTimer();
    void startAcceptPollTimer();
    void stopAcceptPollTimer();
    void startConnectTimeoutTimer();
    void stopConnectTimeoutTimer();
    void onConnectTimer();
    void onAcceptPollTimer();
    void checkAllConnected();
    void onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);
    void onServerProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);

private:
    qsc::AdbProcess m_workProcess;
    qsc::AdbProcess m_serverProcess;
    NativeTcpServer m_serverSocket;        // video socket server
    NativeTcpServer m_serverSocketAudio;   // audio socket server
    NativeTcpServer m_serverSocketCtrl;    // control socket server
    NativeTcpServer m_serverSocketAux;     // auxiliary socket server
    VideoSocket* m_videoSocket = nullptr;
    NativeTcpSocket *m_audioSocket = nullptr;
    NativeTcpSocket *m_controlSocket = nullptr;
    NativeTcpSocket *m_auxSocket = nullptr;
    bool m_tunnelEnabled = false;
    bool m_tunnelForward = false;    // use "adb forward" instead of "adb reverse"
    NativeTimer m_acceptTimeoutTimer;
    NativeTimer m_connectTimeoutTimer;
    NativeTimer m_acceptPollTimer;
    uint32_t m_connectCount = 0;
    uint32_t m_restartCount = 0;
    std::string m_deviceName;
    Size m_deviceSize;
    ServerParams m_params;

    SERVER_START_STEP m_serverStartStep = SSS_NULL;
};

#endif // TCPSERVERHANDLER_H
