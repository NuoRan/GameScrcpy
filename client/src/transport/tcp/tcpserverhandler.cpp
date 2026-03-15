#define LOG_TAG "TcpServer"
#include "Logger.h"
#include "ElapsedTimer.h"
#include "StringUtils.h"
#include "ThreadDispatcher.h"

#include "tcpserverhandler.h"
#include "videosocket.h"
#include "NativeTcpSocket.h"

#define DEVICE_NAME_FIELD_LENGTH 64
#define SOCKET_NAME_PREFIX "scrcpy"
#define MAX_CONNECT_COUNT 30
#define MAX_RESTART_COUNT 1

static uint32_t bufferRead32be(uint8_t *buf)
{
    return static_cast<uint32_t>((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
}

// Helper: build socket base name with optional scid suffix
static std::string makeBaseName(int scid)
{
    if (scid == -1) {
        return SOCKET_NAME_PREFIX;
    }
    return strutil::format(SOCKET_NAME_PREFIX "_%08x", scid);
}

TcpServerHandler::TcpServerHandler()
{
    // 桥接 AdbProcess Signal<> 到本类回调
    m_workProcess.adbProcessResult.connect(
        [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            onWorkProcessResult(processResult);
        });
    m_serverProcess.adbProcessResult.connect(
        [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            onServerProcessResult(processResult);
        });

    // 设置 NativeTimer 回调
    m_acceptTimeoutTimer.setCallback([this]() {
        stopAcceptTimeoutTimer();
        stopAcceptPollTimer();
        serverStarted.fire(false, std::string(), Size());
        onConnectTimer();
    });
    m_acceptPollTimer.setCallback([this]() {
        onAcceptPollTimer();
    });

    // Reverse 模式的连接接受在 onAcceptPollTimer() 中轮询处理
}

TcpServerHandler::~TcpServerHandler() {}

// 确定 reverse 模式下，当前步骤完成后的下一个步骤（跳过禁用通道）
TcpServerHandler::SERVER_START_STEP TcpServerHandler::nextStepAfterReverse(SERVER_START_STEP completed)
{
    if (completed == SSS_ENABLE_TUNNEL_REVERSE) {
        if (m_params.audioEnabled) return SSS_ENABLE_TUNNEL_REVERSE_AUDIO;
        completed = SSS_ENABLE_TUNNEL_REVERSE_AUDIO;
    }
    if (completed == SSS_ENABLE_TUNNEL_REVERSE_AUDIO) {
        if (m_params.control) return SSS_ENABLE_TUNNEL_REVERSE_CTRL;
        completed = SSS_ENABLE_TUNNEL_REVERSE_CTRL;
    }
    if (completed == SSS_ENABLE_TUNNEL_REVERSE_CTRL) {
        if (m_params.auxEnabled) return SSS_ENABLE_TUNNEL_REVERSE_AUX;
        completed = SSS_ENABLE_TUNNEL_REVERSE_AUX;
    }
    return SSS_EXECUTE_SERVER;
}

// 确定 forward 模式下，当前步骤完成后的下一个步骤（跳过禁用通道）
TcpServerHandler::SERVER_START_STEP TcpServerHandler::nextStepAfterForward(SERVER_START_STEP completed)
{
    if (completed == SSS_ENABLE_TUNNEL_FORWARD) {
        if (m_params.audioEnabled) return SSS_ENABLE_TUNNEL_FORWARD_AUDIO;
        completed = SSS_ENABLE_TUNNEL_FORWARD_AUDIO;
    }
    if (completed == SSS_ENABLE_TUNNEL_FORWARD_AUDIO) {
        if (m_params.control) return SSS_ENABLE_TUNNEL_FORWARD_CTRL;
        completed = SSS_ENABLE_TUNNEL_FORWARD_CTRL;
    }
    if (completed == SSS_ENABLE_TUNNEL_FORWARD_CTRL) {
        if (m_params.auxEnabled) return SSS_ENABLE_TUNNEL_FORWARD_AUX;
        completed = SSS_ENABLE_TUNNEL_FORWARD_AUX;
    }
    return SSS_EXECUTE_SERVER;
}

// Reverse 模式：在所有启用的通道上开始监听
bool TcpServerHandler::listenOnEnabledPorts()
{
    // Video 始终监听
    if (!m_serverSocket.listen("127.0.0.1", m_params.localPort)) {
        LOG_E("Could not listen on video port %u", m_params.localPort);
        m_serverStartStep = SSS_NULL;
        disableTunnelReverse();
        serverStarted.fire(false, std::string(), Size());
        return false;
    }
    if (m_params.audioEnabled) {
        if (!m_serverSocketAudio.listen("127.0.0.1", m_params.localPortAudio)) {
            LOG_E("Could not listen on audio port %u", m_params.localPortAudio);
            m_serverSocket.close();
            m_serverStartStep = SSS_NULL;
            disableTunnelReverse();
            serverStarted.fire(false, std::string(), Size());
            return false;
        }
    }
    if (m_params.control) {
        if (!m_serverSocketCtrl.listen("127.0.0.1", m_params.localPortCtrl)) {
            LOG_E("Could not listen on control port %u", m_params.localPortCtrl);
            m_serverSocket.close();
            if (m_params.audioEnabled) m_serverSocketAudio.close();
            m_serverStartStep = SSS_NULL;
            disableTunnelReverse();
            serverStarted.fire(false, std::string(), Size());
            return false;
        }
    }
    if (m_params.auxEnabled) {
        if (!m_serverSocketAux.listen("127.0.0.1", m_params.localPortAux)) {
            LOG_E("Could not listen on aux port %u", m_params.localPortAux);
            m_serverSocket.close();
            if (m_params.audioEnabled) m_serverSocketAudio.close();
            if (m_params.control) m_serverSocketCtrl.close();
            m_serverStartStep = SSS_NULL;
            disableTunnelReverse();
            serverStarted.fire(false, std::string(), Size());
            return false;
        }
    }
    return true;
}

bool TcpServerHandler::pushServer()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    m_workProcess.push(m_params.serial, m_params.serverLocalPath, m_params.serverRemotePath);
    return true;
}

bool TcpServerHandler::enableTunnelReverse()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.reverse(m_params.serial, baseName + "_video", m_params.localPort);
    return true;
}

bool TcpServerHandler::enableTunnelReverseAudio()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.reverse(m_params.serial, baseName + "_audio", m_params.localPortAudio);
    return true;
}

bool TcpServerHandler::enableTunnelReverseCtrl()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.reverse(m_params.serial, baseName + "_control", m_params.localPortCtrl);
    return true;
}

bool TcpServerHandler::enableTunnelReverseAux()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.reverse(m_params.serial, baseName + "_aux", m_params.localPortAux);
    return true;
}

bool TcpServerHandler::disableTunnelReverse()
{
    std::string baseName = makeBaseName(m_params.scid);

    qsc::AdbProcess *adb1 = new qsc::AdbProcess();
    adb1->adbProcessResult.connect([adb1](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb1]() { delete adb1; });
        }
    });
    adb1->reverseRemove(m_params.serial, baseName + "_video");

    qsc::AdbProcess *adb2 = new qsc::AdbProcess();
    adb2->adbProcessResult.connect([adb2](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb2]() { delete adb2; });
        }
    });
    adb2->reverseRemove(m_params.serial, baseName + "_control");

    qsc::AdbProcess *adb3 = new qsc::AdbProcess();
    adb3->adbProcessResult.connect([adb3](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb3]() { delete adb3; });
        }
    });
    adb3->reverseRemove(m_params.serial, baseName + "_aux");

    qsc::AdbProcess *adb4 = new qsc::AdbProcess();
    adb4->adbProcessResult.connect([adb4](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb4]() { delete adb4; });
        }
    });
    adb4->reverseRemove(m_params.serial, baseName + "_audio");
    return true;
}

bool TcpServerHandler::enableTunnelForward()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.forward(m_params.serial, m_params.localPort, baseName + "_video");
    return true;
}

bool TcpServerHandler::enableTunnelForwardAudio()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.forward(m_params.serial, m_params.localPortAudio, baseName + "_audio");
    return true;
}

bool TcpServerHandler::enableTunnelForwardCtrl()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.forward(m_params.serial, m_params.localPortCtrl, baseName + "_control");
    return true;
}

bool TcpServerHandler::enableTunnelForwardAux()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    std::string baseName = makeBaseName(m_params.scid);
    m_workProcess.forward(m_params.serial, m_params.localPortAux, baseName + "_aux");
    return true;
}

bool TcpServerHandler::disableTunnelForward()
{
    qsc::AdbProcess *adb1 = new qsc::AdbProcess();
    adb1->adbProcessResult.connect([adb1](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb1]() { delete adb1; });
        }
    });
    adb1->forwardRemove(m_params.serial, m_params.localPort);

    qsc::AdbProcess *adb2 = new qsc::AdbProcess();
    adb2->adbProcessResult.connect([adb2](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb2]() { delete adb2; });
        }
    });
    adb2->forwardRemove(m_params.serial, m_params.localPortCtrl);

    qsc::AdbProcess *adb3 = new qsc::AdbProcess();
    adb3->adbProcessResult.connect([adb3](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb3]() { delete adb3; });
        }
    });
    adb3->forwardRemove(m_params.serial, m_params.localPortAux);

    qsc::AdbProcess *adb4 = new qsc::AdbProcess();
    adb4->adbProcessResult.connect([adb4](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
            dispatch::postToMain([adb4]() { delete adb4; });
        }
    });
    adb4->forwardRemove(m_params.serial, m_params.localPortAudio);
    return true;
}

bool TcpServerHandler::execute()
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

    if (1 == m_params.captureOrientationLock) {
        args.push_back("capture_orientation=@" + std::to_string(m_params.captureOrientation));
    } else if (2 == m_params.captureOrientationLock) {
        args.push_back("capture_orientation=@");
    } else {
        args.push_back("capture_orientation=" + std::to_string(m_params.captureOrientation));
    }
    if (m_tunnelForward) {
        args.push_back("tunnel_forward=true");
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
        // 覆盖: 通知服务端不要捕获音频
        args.pop_back();
        args.push_back("audio=false");
    }
    if (!m_params.auxEnabled) {
        args.push_back("aux=false");
    }
    if (-1 != m_params.scid) {
        args.push_back(strutil::format("scid=%08x", m_params.scid));
    }

#ifdef SERVER_DEBUGGER
    LOG_I("Server debugger waiting for a client on device port " SERVER_DEBUGGER_PORT "...");
#endif

    m_serverProcess.execute(m_params.serial, args);
    return true;
}

bool TcpServerHandler::start(TcpServerHandler::ServerParams params)
{
    m_params = params;
    LOGI() << "TcpServerHandler: Starting USB/TCP mode for" << m_params.serial;
    m_serverStartStep = SSS_PUSH;
    return startServerByStep();
}

bool TcpServerHandler::connectTo()
{
    if (SSS_RUNNING != m_serverStartStep) {
        LOG_W("server not run");
        return false;
    }

    if (!m_tunnelForward && !m_videoSocket) {
        startAcceptTimeoutTimer();
        startAcceptPollTimer();
        return true;
    }

    startConnectTimeoutTimer();
    return true;
}

bool TcpServerHandler::isReverse()
{
    return !m_tunnelForward;
}

TcpServerHandler::ServerParams TcpServerHandler::getParams()
{
    return m_params;
}

VideoSocket* TcpServerHandler::removeVideoSocket()
{
    VideoSocket* socket = m_videoSocket;
    m_videoSocket = nullptr;
    return socket;
}

NativeTcpSocket *TcpServerHandler::getControlSocket()
{
    return m_controlSocket;
}

NativeTcpSocket *TcpServerHandler::getAudioSocket()
{
    return m_audioSocket;
}

NativeTcpSocket *TcpServerHandler::getAuxSocket()
{
    return m_auxSocket;
}

void TcpServerHandler::stop()
{
    if (m_tunnelForward) {
        stopConnectTimeoutTimer();
    } else {
        stopAcceptTimeoutTimer();
        stopAcceptPollTimer();
    }

    if (m_videoSocket) {
        delete m_videoSocket;
        m_videoSocket = nullptr;
    }
    if (m_controlSocket) {
        m_controlSocket->close();
        delete m_controlSocket;
        m_controlSocket = nullptr;
    }
    if (m_audioSocket) {
        m_audioSocket->close();
        delete m_audioSocket;
        m_audioSocket = nullptr;
    }
    if (m_auxSocket) {
        m_auxSocket->close();
        delete m_auxSocket;
        m_auxSocket = nullptr;
    }
    m_serverProcess.kill();
    if (m_tunnelEnabled) {
        if (m_tunnelForward) {
            disableTunnelForward();
        } else {
            disableTunnelReverse();
        }
        m_tunnelForward = false;
        m_tunnelEnabled = false;
    }
    m_serverSocket.close();
    m_serverSocketAudio.close();
    m_serverSocketCtrl.close();
    m_serverSocketAux.close();
}

bool TcpServerHandler::startServerByStep()
{
    bool stepSuccess = false;
    if (SSS_NULL != m_serverStartStep) {
        switch (m_serverStartStep) {
        case SSS_PUSH:
            stepSuccess = pushServer();
            break;
        case SSS_ENABLE_TUNNEL_REVERSE:
            stepSuccess = enableTunnelReverse();
            break;
        case SSS_ENABLE_TUNNEL_REVERSE_AUDIO:
            stepSuccess = enableTunnelReverseAudio();
            break;
        case SSS_ENABLE_TUNNEL_REVERSE_CTRL:
            stepSuccess = enableTunnelReverseCtrl();
            break;
        case SSS_ENABLE_TUNNEL_REVERSE_AUX:
            stepSuccess = enableTunnelReverseAux();
            break;
        case SSS_ENABLE_TUNNEL_FORWARD:
            stepSuccess = enableTunnelForward();
            break;
        case SSS_ENABLE_TUNNEL_FORWARD_AUDIO:
            stepSuccess = enableTunnelForwardAudio();
            break;
        case SSS_ENABLE_TUNNEL_FORWARD_CTRL:
            stepSuccess = enableTunnelForwardCtrl();
            break;
        case SSS_ENABLE_TUNNEL_FORWARD_AUX:
            stepSuccess = enableTunnelForwardAux();
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

bool TcpServerHandler::readInfo(VideoSocket *videoSocket, std::string &deviceName, Size &size)
{
    unsigned char buf[DEVICE_NAME_FIELD_LENGTH + 12];
    int len = videoSocket->readInfoData(buf, sizeof(buf), 3000);
    if (len < DEVICE_NAME_FIELD_LENGTH + 12) {
        LOG_I("Could not retrieve device information");
        return false;
    }
    buf[DEVICE_NAME_FIELD_LENGTH - 1] = '\0';
    deviceName = std::string(reinterpret_cast<const char *>(buf));

    size.width = bufferRead32be(&buf[DEVICE_NAME_FIELD_LENGTH + 4]);
    size.height = bufferRead32be(&buf[DEVICE_NAME_FIELD_LENGTH + 8]);

    return true;
}

void TcpServerHandler::checkAllConnected()
{
    // Video 始终必需
    if (!m_videoSocket || !m_videoSocket->isValid()) return;
    // 仅检查已启用的通道
    if (m_params.audioEnabled && (!m_audioSocket || !m_audioSocket->isValid())) return;
    if (m_params.control && (!m_controlSocket || !m_controlSocket->isValid())) return;
    if (m_params.auxEnabled && (!m_auxSocket || !m_auxSocket->isValid())) return;

    stopAcceptTimeoutTimer();
    stopAcceptPollTimer();
    disableTunnelReverse();
    m_tunnelEnabled = false;
    serverStarted.fire(true, m_deviceName, m_deviceSize);
}

void TcpServerHandler::startAcceptTimeoutTimer()
{
    m_acceptTimeoutTimer.stop();
    m_acceptTimeoutTimer.start(5000);  // 5s — 首次启动 scrcpy-server 需更长时间
}

void TcpServerHandler::stopAcceptTimeoutTimer()
{
    m_acceptTimeoutTimer.stop();
}

void TcpServerHandler::startConnectTimeoutTimer()
{
    m_connectTimeoutTimer.stop();
    m_connectTimeoutTimer.start(300);
}

void TcpServerHandler::stopConnectTimeoutTimer()
{
    m_connectTimeoutTimer.stop();
    m_connectCount = 0;
}

void TcpServerHandler::startAcceptPollTimer()
{
    m_acceptPollTimer.stop();
    m_acceptPollTimer.start(50);  // 每 50ms 轮询一次
}

void TcpServerHandler::stopAcceptPollTimer()
{
    m_acceptPollTimer.stop();
}

void TcpServerHandler::onAcceptPollTimer()
{
    // 尝试接受 video socket
    if (!m_videoSocket) {
        NativeTcpSocket *ns = m_serverSocket.tryAccept();
        if (ns) {
            VideoSocket *vs = new VideoSocket();
            vs->adoptSocket(ns->release());
            delete ns;
            if (!vs->isValid() || !readInfo(vs, m_deviceName, m_deviceSize)) {
                delete vs;
                stop();
                serverStarted.fire(false, std::string(), Size());
                return;
            }
            m_videoSocket = vs;
            m_serverSocket.close();
            checkAllConnected();
        }
    }

    // 尝试接受 audio socket (仅在启用时)
    if (m_params.audioEnabled && !m_audioSocket) {
        NativeTcpSocket *ns = m_serverSocketAudio.tryAccept();
        if (ns) {
            ns->setNoDelay(true);
            m_audioSocket = ns;
            m_serverSocketAudio.close();
            checkAllConnected();
        }
    }

    // 尝试接受 control socket (仅在启用时)
    if (m_params.control && !m_controlSocket) {
        NativeTcpSocket *ns = m_serverSocketCtrl.tryAccept();
        if (ns) {
            ns->setNoDelay(true);
            m_controlSocket = ns;
            m_serverSocketCtrl.close();
            checkAllConnected();
        }
    }

    // 尝试接受 auxiliary socket (仅在启用时)
    if (m_params.auxEnabled && !m_auxSocket) {
        NativeTcpSocket *ns = m_serverSocketAux.tryAccept();
        if (ns) {
            ns->setNoDelay(true);
            m_auxSocket = ns;
            m_serverSocketAux.close();
            checkAllConnected();
        }
    }
}

void TcpServerHandler::onConnectTimer()
{
    std::string deviceName;
    Size deviceSize;
    bool success = false;

    VideoSocket *videoSocket = new VideoSocket();
    NativeTcpSocket *audioSocket = nullptr;
    NativeTcpSocket *controlSocket = nullptr;
    NativeTcpSocket *auxSocket = nullptr;

    // Video: 使用原生 socket 连接
    if (!videoSocket->connectToHost("127.0.0.1", m_params.localPort, 1000)) {
        m_connectCount = MAX_CONNECT_COUNT;
        LOG_W("video socket connect to server failed");
        goto result;
    }

    if (m_params.audioEnabled) {
        audioSocket = new NativeTcpSocket();
        if (!audioSocket->connectToHost("127.0.0.1", m_params.localPortAudio, 1000)) {
            m_connectCount = MAX_CONNECT_COUNT;
            LOG_W("audio socket connect to server failed");
            goto result;
        }
        audioSocket->setNoDelay(true);
    }

    if (m_params.control) {
        controlSocket = new NativeTcpSocket();
        if (!controlSocket->connectToHost("127.0.0.1", m_params.localPortCtrl, 1000)) {
            m_connectCount = MAX_CONNECT_COUNT;
            LOG_W("control socket connect to server failed");
            goto result;
        }
        controlSocket->setNoDelay(true);
    }

    if (m_params.auxEnabled) {
        auxSocket = new NativeTcpSocket();
        if (!auxSocket->connectToHost("127.0.0.1", m_params.localPortAux, 1000)) {
            m_connectCount = MAX_CONNECT_COUNT;
            LOG_W("aux socket connect to server failed");
            goto result;
        }
        auxSocket->setNoDelay(true);
    }

    // 读取 1 字节连接响应 + 设备信息
    {
        char connectByte = 0;
        if (!videoSocket->nativeSocket().recvAll(&connectByte, 1)) {
            LOG_W("video socket read connect byte failed, try again");
            goto result;
        }
        if (readInfo(videoSocket, deviceName, deviceSize)) {
            success = true;
            goto result;
        } else {
            LOG_W("video socket connect to server read device info failed, try again");
            goto result;
        }
    }

result:
    if (success) {
        stopConnectTimeoutTimer();
        m_videoSocket = videoSocket;
        if (audioSocket) { char dummy; audioSocket->recv(&dummy, 1); m_audioSocket = audioSocket; }
        if (controlSocket) { char dummy; controlSocket->recv(&dummy, 1); m_controlSocket = controlSocket; }
        if (auxSocket) { char dummy; auxSocket->recv(&dummy, 1); m_auxSocket = auxSocket; }
        disableTunnelForward();
        m_tunnelEnabled = false;
        m_restartCount = 0;
        serverStarted.fire(success, deviceName, deviceSize);
        return;
    }

    delete videoSocket;
    delete audioSocket;
    delete controlSocket;
    delete auxSocket;

    if (MAX_CONNECT_COUNT <= m_connectCount++) {
        stopConnectTimeoutTimer();
        stop();
        if (MAX_RESTART_COUNT > m_restartCount++) {
            LOG_W("restart server auto");
            start(m_params);
        } else {
            m_restartCount = 0;
            serverStarted.fire(false, std::string(), Size());
        }
    }
}

void TcpServerHandler::onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult)
{
    if (SSS_NULL != m_serverStartStep) {
        switch (m_serverStartStep) {
        case SSS_PUSH:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    if (m_params.useReverse) {
                        m_serverStartStep = SSS_ENABLE_TUNNEL_REVERSE;
                    } else {
                        m_tunnelForward = true;
                        m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                    }
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb push failed");
                    m_serverStartStep = SSS_NULL;
                    serverStarted.fire(false, std::string(), Size());
                }
                break;

            // ─── Reverse 模式: 依次建立启用的通道 ───
            case SSS_ENABLE_TUNNEL_REVERSE:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = nextStepAfterReverse(SSS_ENABLE_TUNNEL_REVERSE);
                    if (m_serverStartStep == SSS_EXECUTE_SERVER) {
                        if (!listenOnEnabledPorts()) break;
                    }
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb reverse (video) failed, try forward");
                    m_tunnelForward = true;
                    m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                    startServerByStep();
                }
                break;
            case SSS_ENABLE_TUNNEL_REVERSE_AUDIO:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = nextStepAfterReverse(SSS_ENABLE_TUNNEL_REVERSE_AUDIO);
                    if (m_serverStartStep == SSS_EXECUTE_SERVER) {
                        if (!listenOnEnabledPorts()) break;
                    }
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb reverse (audio) failed, try forward");
                    disableTunnelReverse();
                    m_tunnelForward = true;
                    m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                    startServerByStep();
                }
                break;
            case SSS_ENABLE_TUNNEL_REVERSE_CTRL:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = nextStepAfterReverse(SSS_ENABLE_TUNNEL_REVERSE_CTRL);
                    if (m_serverStartStep == SSS_EXECUTE_SERVER) {
                        if (!listenOnEnabledPorts()) break;
                    }
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb reverse (control) failed, try forward");
                    disableTunnelReverse();
                    m_tunnelForward = true;
                    m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                    startServerByStep();
                }
                break;
            case SSS_ENABLE_TUNNEL_REVERSE_AUX:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    // AUX 是最后可能的 reverse 步骤，直接进入 listen + execute
                    if (!listenOnEnabledPorts()) break;
                    m_serverStartStep = SSS_EXECUTE_SERVER;
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb reverse (aux) failed, try forward");
                    disableTunnelReverse();
                    m_tunnelForward = true;
                    m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                    startServerByStep();
                }
                break;

            // ─── Forward 模式: 依次建立启用的通道 ───
            case SSS_ENABLE_TUNNEL_FORWARD:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = nextStepAfterForward(SSS_ENABLE_TUNNEL_FORWARD);
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb forward (video) failed");
                    m_serverStartStep = SSS_NULL;
                    serverStarted.fire(false, std::string(), Size());
                }
                break;
            case SSS_ENABLE_TUNNEL_FORWARD_AUDIO:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = nextStepAfterForward(SSS_ENABLE_TUNNEL_FORWARD_AUDIO);
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb forward (audio) failed");
                    disableTunnelForward();
                    m_serverStartStep = SSS_NULL;
                    serverStarted.fire(false, std::string(), Size());
                }
                break;
            case SSS_ENABLE_TUNNEL_FORWARD_CTRL:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = nextStepAfterForward(SSS_ENABLE_TUNNEL_FORWARD_CTRL);
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb forward (control) failed");
                    disableTunnelForward();
                    m_serverStartStep = SSS_NULL;
                    serverStarted.fire(false, std::string(), Size());
                }
                break;
            case SSS_ENABLE_TUNNEL_FORWARD_AUX:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = SSS_EXECUTE_SERVER;
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    LOG_E("adb forward (aux) failed");
                    disableTunnelForward();
                    m_serverStartStep = SSS_NULL;
                    serverStarted.fire(false, std::string(), Size());
                }
                break;
            default:
                break;
            }
        }
}

void TcpServerHandler::onServerProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult)
{
    if (SSS_EXECUTE_SERVER == m_serverStartStep) {
        if (qsc::AdbProcess::AER_SUCCESS_START == processResult) {
            m_serverStartStep = SSS_RUNNING;
            m_tunnelEnabled = true;
            connectTo();
        } else if (qsc::AdbProcess::AER_ERROR_START == processResult) {
            if (!m_tunnelForward) {
                m_serverSocket.close();
                if (m_params.audioEnabled) m_serverSocketAudio.close();
                if (m_params.control) m_serverSocketCtrl.close();
                if (m_params.auxEnabled) m_serverSocketAux.close();
                disableTunnelReverse();
            } else {
                disableTunnelForward();
            }
            LOG_E("adb shell start server failed");
            m_serverStartStep = SSS_NULL;
            serverStarted.fire(false, std::string(), Size());
        }
    } else if (SSS_RUNNING == m_serverStartStep) {
        m_serverStartStep = SSS_NULL;
        serverStoped.fire();
    }
}
