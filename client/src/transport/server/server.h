#ifndef SERVER_H
#define SERVER_H

#include "GameSignal.h"

#include <string>
#include <cstdint>
#include "GameTypes.h"

// 前向声明
class KcpServer;
class NativeTcpSocket;
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
class Server
{

public:
    struct ServerParams
    {
        // 必需参数 / Required parameters
        std::string serial;              // 设备序列号 / Device serial
        std::string serverLocalPath;     // 本地 server 路径 / Local server path

        // 可选参数 / Optional parameters
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

        // TCP 模式参数 / TCP mode parameters
        uint16_t localPort = 27183;        // TCP 本地端口 (USB 模式) / TCP local port (USB)
        uint16_t localPortAudio = 27186;   // TCP 音频端口 / TCP audio port
        uint16_t localPortCtrl = 27184;    // TCP 控制端口 / TCP control port
        uint16_t localPortAux = 27185;     // TCP 辅助通道端口 / TCP aux channel port
        bool useReverse = true;           // TCP 模式: 先尝试 reverse / TCP: try reverse first

        // KCP 模式参数 / KCP mode parameters
        uint16_t kcpPort = 27185;          // KCP UDP 视频端口 / KCP UDP video port (ctrl = kcpPort+1)

        int32_t scid = -1;
    };

    explicit Server();
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
    NativeTcpSocket *getControlSocket();
    NativeTcpSocket *getAudioTcpSocket();  // USB 或 WiFi 模式都可用
    NativeTcpSocket *getAuxTcpSocket();    // USB 或 WiFi 模式都可用

    // 获取服务器IP (WiFi 模式)
    std::string getServerIp() const;

    // 信号 (Signal<>) — 替代 Qt signals
    Signal<bool, const std::string&, const Size&> serverStarted;
    Signal<> serverStoped;

private:
    bool m_useKcp = false;
    ServerParams m_params;

    // 内部实现 (互斥)
    KcpServer* m_kcpServer = nullptr;
    TcpServerHandler* m_tcpServer = nullptr;
};

#endif // SERVER_H
