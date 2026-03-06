#define LOG_TAG "KcpServer"
#include "Logger.h"
#include "NativeTcpSocket.h"
#include "StringUtils.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")


#include "kcpserver.h"

#define MAX_WAIT_COUNT 100  // 最多等待 100 * 100ms = 10秒
#define MAX_RESTART_COUNT 1

KcpServer::KcpServer()
{
    m_workProcess.adbProcessResult.connect(
        [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            onWorkProcessResult(processResult);
        });
    m_serverProcess.adbProcessResult.connect(
        [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            onServerProcessResult(processResult);
        });

    // 设置 NativeTimer 回调
    m_waitTimer.setCallback([this]() {
        onWaitKcpTimer();
    });
}

KcpServer::~KcpServer() {}

bool KcpServer::killOldServer()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    // 杀死设备上旧的 scrcpy 进程，避免端口占用
    std::vector<std::string> args = {"shell", "pkill", "-f", "scrcpy"};
    m_workProcess.execute(m_params.serial, args);
    return true;
}

bool KcpServer::pushServer()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    m_workProcess.push(m_params.serial, m_params.serverLocalPath, m_params.serverRemotePath);
    return true;
}

bool KcpServer::execute()
{
    if (m_serverProcess.isRuning()) {
        m_serverProcess.kill();
    }
    std::vector<std::string> args;
    args.push_back("shell");
    args.push_back("CLASSPATH=" + m_params.serverRemotePath);
    args.push_back("app_process");

#ifdef SERVER_DEBUGGER
#define SERVER_DEBUGGER_PORT "5005"
    args.push_back(
#ifdef SERVER_DEBUGGER_METHOD_NEW
        "-XjdwpProvider:internal -XjdwpOptions:transport=dt_socket,suspend=y,server=y,address="
#else
        "-agentlib:jdwp=transport=dt_socket,suspend=y,server=y,address="
#endif
        SERVER_DEBUGGER_PORT);
#endif

    args.push_back("/");
    args.push_back("com.genymobile.scrcpy.Server");
    args.push_back(m_params.serverVersion);

    args.push_back("video_bit_rate=" + std::to_string(m_params.bitRate));
    if (!m_params.logLevel.empty()) {
        args.push_back("log_level=" + m_params.logLevel);
    }
    if (m_params.maxSize > 0) {
        args.push_back("max_size=" + std::to_string(m_params.maxSize));
    }
    if (m_params.maxFps > 0) {
        args.push_back("max_fps=" + std::to_string(m_params.maxFps));
    }

    // capture_orientation
    if (1 == m_params.captureOrientationLock) {
        args.push_back("capture_orientation=@" + std::to_string(m_params.captureOrientation));
    } else if (2 == m_params.captureOrientationLock) {
        args.push_back("capture_orientation=@");
    } else {
        args.push_back("capture_orientation=" + std::to_string(m_params.captureOrientation));
    }

    if (!m_params.crop.empty()) {
        args.push_back("crop=" + m_params.crop);
    }
    if (!m_params.control) {
        args.push_back("control=false");
    }
    if (m_params.stayAwake) {
        args.push_back("stay_awake=true");
    }
    if (!m_params.codecOptions.empty()) {
        args.push_back("video_codec_options=" + m_params.codecOptions);
    }
    if (!m_params.codecName.empty()) {
        args.push_back("video_encoder=" + m_params.codecName);
    }
    if (m_params.videoCodec != "h264") {
        args.push_back("video_codec=" + m_params.videoCodec);
    }
    args.push_back("audio=true");
    if (!m_params.audioEnabled) {
        args.pop_back();
        args.push_back("audio=false");
    }
    if (-1 != m_params.scid) {
        args.push_back(strutil::format("scid=%08x", m_params.scid));
    }

    // KCP 模式参数
    args.push_back("use_kcp=true");
    args.push_back("kcp_port=" + std::to_string(m_params.kcpPort));
    args.push_back("kcp_control_port=" + std::to_string(m_params.kcpPort + 1));
    args.push_back("aux_port=" + std::to_string(m_params.kcpPort + 2)); // TCP 端口: 音频 + 辅助共用

    // Extract device IP from serial (e.g. "192.168.1.100:5555" → "192.168.1.100")
    std::string deviceIp = m_params.serial;
    auto colonPos = deviceIp.find(':');
    if (colonPos != std::string::npos) {
        deviceIp = deviceIp.substr(0, colonPos);
    }
    std::string clientIp = findClientIpInSameSubnet(deviceIp);
    args.push_back("client_ip=" + clientIp);

#ifdef SERVER_DEBUGGER
    LOG_I("Server debugger waiting for a client on device port " SERVER_DEBUGGER_PORT "...");
#endif

    m_serverProcess.execute(m_params.serial, args);
    return true;
}

bool KcpServer::start(KcpServer::ServerParams params)
{
    m_params = params;
    LOGI() << "KcpServer: Starting WiFi/KCP mode for" << m_params.serial;
    m_serverStartStep = SSS_KILL_SERVER;  // 先杀死旧进程
    return startServerByStep();
}

KcpServer::ServerParams KcpServer::getParams()
{
    return m_params;
}

KcpVideoSocket* KcpServer::removeKcpVideoSocket()
{
    KcpVideoSocket* socket = m_kcpVideoSocket;
    m_kcpVideoSocket = nullptr;
    return socket;
}

KcpControlSocket* KcpServer::getKcpControlSocket()
{
    return m_kcpControlSocket;
}

void KcpServer::stop()
{
    stopWaitTimer();

    if (m_audioTcpSocket) {
        m_audioTcpSocket->close();
        delete m_audioTcpSocket;
        m_audioTcpSocket = nullptr;
    }
    if (m_auxTcpSocket) {
        m_auxTcpSocket->close();
        delete m_auxTcpSocket;
        m_auxTcpSocket = nullptr;
    }
    if (m_kcpControlSocket) {
        m_kcpControlSocket->close();
        delete m_kcpControlSocket;
        m_kcpControlSocket = nullptr;
    }
    if (m_kcpVideoSocket) {
        m_kcpVideoSocket->close();
        delete m_kcpVideoSocket;
        m_kcpVideoSocket = nullptr;
    }

    m_serverProcess.kill();
}

bool KcpServer::startServerByStep()
{
    bool stepSuccess = false;
    if (SSS_NULL != m_serverStartStep) {
        switch (m_serverStartStep) {
        case SSS_KILL_SERVER:
            stepSuccess = killOldServer();
            break;
        case SSS_PUSH:
            stepSuccess = pushServer();
            break;
        case SSS_EXECUTE_SERVER:
            stepSuccess = execute();
            break;
        default:
            break;
        }
    }

    if (!stepSuccess) {
        serverStarted.fire(false, std::string(), Size());
    }
    return stepSuccess;
}

std::string KcpServer::findClientIpInSameSubnet(const std::string &deviceIp) const
{
    std::string clientIp;
    // Split deviceIp by '.'
    std::vector<std::string> devParts;
    {
        std::string tmp = deviceIp;
        size_t pos;
        while ((pos = tmp.find('.')) != std::string::npos) {
            devParts.push_back(tmp.substr(0, pos));
            tmp = tmp.substr(pos + 1);
        }
        devParts.push_back(tmp);
    }
    if (devParts.size() != 4) return clientIp;

    ULONG bufLen = 15000;
    std::vector<uint8_t> buf(bufLen);
    auto adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());

    ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST,
                                     nullptr, adapters, &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufLen);
        adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST,
                                   nullptr, adapters, &bufLen);
    }

    if (ret != NO_ERROR) return clientIp;

    std::string fallbackIp;
    for (auto adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) continue;
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        for (auto addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
            if (addr->Address.lpSockaddr->sa_family != AF_INET) continue;

            auto sa = reinterpret_cast<sockaddr_in *>(addr->Address.lpSockaddr);
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa->sin_addr, ipStr, sizeof(ipStr));
            std::string ip(ipStr);

            // Remember first non-loopback as fallback
            if (fallbackIp.empty()) fallbackIp = ip;

            // Check same /24 subnet — split ip by '.'
            std::vector<std::string> pcParts;
            {
                std::string tmp = ip;
                size_t pos;
                while ((pos = tmp.find('.')) != std::string::npos) {
                    pcParts.push_back(tmp.substr(0, pos));
                    tmp = tmp.substr(pos + 1);
                }
                pcParts.push_back(tmp);
            }
            if (pcParts.size() == 4 &&
                pcParts[0] == devParts[0] && pcParts[1] == devParts[1] && pcParts[2] == devParts[2]) {
                return ip;
            }
        }
    }

    return fallbackIp.empty() ? clientIp : fallbackIp;
}

void KcpServer::startWaitTimer()
{
    m_waitTimer.stop();
    m_waitCount = 0;
    m_waitTimer.start(100);
}

void KcpServer::stopWaitTimer()
{
    m_waitTimer.stop();
    m_waitCount = 0;
}

void KcpServer::setupKcpSockets()
{
    static constexpr char kAudioHandshake[] = "QSAU";
    static constexpr int kAudioHandshakeLen = 4;
    static constexpr char kAuxHandshake[] = "QSAX";
    static constexpr int kAuxHandshakeLen = 4;

    // Extract server IP from serial
    std::string serverIp = m_params.serial;
    auto colonPos = serverIp.find(':');
    if (colonPos != std::string::npos) {
        serverIp = serverIp.substr(0, colonPos);
    }

    // 1. 创建 KCP video socket (UDP)
    m_kcpVideoSocket = new KcpVideoSocket();
    m_kcpVideoSocket->setBitrate(m_params.bitRate, m_params.maxFps);
    if (!m_kcpVideoSocket->bind(m_params.kcpPort)) {
        LOGE() << "Failed to bind KCP video socket to port" << m_params.kcpPort;
        delete m_kcpVideoSocket;
        m_kcpVideoSocket = nullptr;
        serverStarted.fire(false, std::string(), Size());
        return;
    }

    // 2. 创建 KCP control socket
    m_kcpControlSocket = new KcpControlSocket();
    if (!m_kcpControlSocket->bind(m_params.kcpPort + 1)) {
        LOGE() << "Failed to bind KCP control socket to port" << (m_params.kcpPort + 1);
        delete m_kcpVideoSocket;
        m_kcpVideoSocket = nullptr;
        serverStarted.fire(false, std::string(), Size());
        return;
    }

    m_kcpControlSocket->connectToHost(serverIp, m_params.kcpPort + 1);

    // 3. 创建 TCP 音频连接（第 1 个连接）+ 辅助通道连接（第 2 个连接）
    //    服务器在 auxPort (kcpPort+2) 开了一个 TCP ServerSocket，按顺序 accept
    uint16_t tcpPort = m_params.kcpPort + 2;

    if (m_params.audioEnabled) {
        LOGI() << "KcpServer: Connecting TCP audio to" << serverIp.c_str() << ":" << tcpPort;
        m_audioTcpSocket = new NativeTcpSocket();
        if (!m_audioTcpSocket->connectToHost(serverIp, tcpPort, 10000)) {
            LOGW() << "KcpServer: Failed to connect audio TCP socket";
            delete m_audioTcpSocket;
            m_audioTcpSocket = nullptr;
        } else {
            m_audioTcpSocket->setNoDelay(true);
            int hsWritten = m_audioTcpSocket->send(kAudioHandshake, kAudioHandshakeLen);
            if (hsWritten == kAudioHandshakeLen) {
                LOGI() << "KcpServer: Audio TCP handshake sent";
            } else {
                LOGW() << "KcpServer: Audio TCP handshake write failed";
            }
            LOGI() << "KcpServer: Audio TCP connected";
        }
    } else {
        LOGI() << "KcpServer: Audio channel disabled, skipping TCP audio";
    }

    if (m_params.auxEnabled) {
        LOGI() << "KcpServer: Connecting TCP aux to" << serverIp.c_str() << ":" << tcpPort;
        m_auxTcpSocket = new NativeTcpSocket();
        if (!m_auxTcpSocket->connectToHost(serverIp, tcpPort, 10000)) {
            LOGW() << "KcpServer: Failed to connect aux TCP socket";
            delete m_auxTcpSocket;
            m_auxTcpSocket = nullptr;
        } else {
            m_auxTcpSocket->setNoDelay(true);
            int hsWritten = m_auxTcpSocket->send(kAuxHandshake, kAuxHandshakeLen);
            if (hsWritten == kAuxHandshakeLen) {
                LOGI() << "KcpServer: Aux TCP handshake sent";
            } else {
                LOGW() << "KcpServer: Aux TCP handshake write failed";
            }
            LOGI() << "KcpServer: Aux TCP connected";
        }
    } else {
        LOGI() << "KcpServer: Aux channel disabled, skipping TCP aux";
    }

    startWaitTimer();
}

void KcpServer::onWaitKcpTimer()
{
    if (m_kcpVideoSocket && m_kcpVideoSocket->isValid()) {
        int64_t avail = m_kcpVideoSocket->bytesAvailable();
        if (avail > 0 || m_waitCount >= 10) {
            stopWaitTimer();
            m_restartCount = 0;
            serverStarted.fire(true, m_deviceName, m_deviceSize);
            return;
        }
    }

    if (MAX_WAIT_COUNT <= m_waitCount++) {
        stopWaitTimer();
        stop();
        if (MAX_RESTART_COUNT > m_restartCount++) {
            start(m_params);
        } else {
            m_restartCount = 0;
            serverStarted.fire(false, std::string(), Size());
        }
    }
}

void KcpServer::onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult)
{
    if (SSS_NULL != m_serverStartStep) {
        switch (m_serverStartStep) {
        case SSS_KILL_SERVER:
            // 无论成功失败都继续（可能没有旧进程）
            if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult ||
                qsc::AdbProcess::AER_ERROR_EXEC == processResult) {
                m_serverStartStep = SSS_PUSH;
                startServerByStep();
            } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                // 等待命令完成
            }
            break;
        case SSS_PUSH:
            if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                m_serverStartStep = SSS_EXECUTE_SERVER;
                startServerByStep();
            } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                LOG_E("adb push failed");
                m_serverStartStep = SSS_NULL;
                serverStarted.fire(false, std::string(), Size());
            }
            break;
        default:
            break;
        }
    }
}

void KcpServer::onServerProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult)
{
    if (SSS_EXECUTE_SERVER == m_serverStartStep) {
        if (qsc::AdbProcess::AER_SUCCESS_START == processResult) {
            m_serverStartStep = SSS_RUNNING;
            setupKcpSockets();
        } else if (qsc::AdbProcess::AER_ERROR_START == processResult) {
            LOG_E("adb shell start server failed");
            m_serverStartStep = SSS_NULL;
            serverStarted.fire(false, std::string(), Size());
        }
    } else if (SSS_RUNNING == m_serverStartStep) {
        m_serverStartStep = SSS_NULL;
        serverStoped.fire();
    }
}
