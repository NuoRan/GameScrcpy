#define LOG_TAG "DeviceManage"
#include "Logger.h"
#include <regex>

#include "devicemanage.h"
#include "demuxer.h"
#include "server.h"
#include "kcpvideosocket.h"
#include "kcpcontrolsocket.h"
#include "videosocket.h"
#include "adbprocess.h"
#include "AuxChannelClient.h"
#include "AudioStreamManager.h"
#include "ConfigCenter.h"
#include "ThreadDispatcher.h"

// 新架构
#include "service/DeviceSession.h"
#include "service/ZeroCopyStreamManager.h"
#include "service/InputManager.h"
#include "infra/FrameData.h"
#include "infra/SessionParams.h"

namespace qsc {

#define DM_MAX_DEVICES_NUM 1000

// ============================================================================
// DeviceController 实现
// ============================================================================

DeviceController::DeviceController(const DeviceParams& params)
    : m_params(params)
{
    m_aliveToken = std::make_shared<std::atomic<bool>>(true);

    // 初始化 ADB 进程（用于获取手机实际分辨率）
    m_adbSizeProcess = new AdbProcess();
    m_adbSizeProcess->adbProcessResult.connect(
        [this](AdbProcess::ADB_EXEC_RESULT processResult) {
            auto token = m_aliveToken;
            dispatch::postToMain([this, token, processResult]() {
                if (!token || !token->load(std::memory_order_acquire)) return;
                onAdbSizeResult(processResult);
            });
        });

    // 转换参数
    core::SessionParams sessionParams;
    sessionParams.serial = params.serial;
    sessionParams.serverLocalPath = params.serverLocalPath;
    sessionParams.maxSize = params.maxSize;
    sessionParams.bitRate = params.bitRate;
    sessionParams.maxFps = params.maxFps;
    sessionParams.useKcp = params.serial.find(':') != std::string::npos;
    sessionParams.deviceIP = sessionParams.useKcp ?
        params.serial.substr(0, params.serial.find(':')) : std::string();
    sessionParams.kcpPort = params.kcpPort;
    sessionParams.tcpPort = params.localPort;
    sessionParams.useReverse = params.useReverse;
    sessionParams.serverRemotePath = params.serverRemotePath;
    sessionParams.serverVersion = params.serverVersion;
    sessionParams.logLevel = params.logLevel;
    sessionParams.codecOptions = params.codecOptions;
    sessionParams.codecName = params.codecName;
    sessionParams.videoCodec = params.videoCodec;
    sessionParams.closeScreen = params.closeScreen;
    sessionParams.keyMapJson = params.gameScript;
    sessionParams.frameSize = Size(params.maxSize, params.maxSize);

    // 创建 DeviceSession
    m_session = std::make_unique<core::DeviceSession>(sessionParams);

    // 创建零拷贝视频管线
    m_streamManager = std::make_unique<core::ZeroCopyStreamManager>();

    // 将 FrameQueue 传递给 DeviceSession
    m_session->setFrameQueue(m_streamManager->frameQueue());

    // 连接流管理器信号 (Signal<>)
    m_streamManager->fpsUpdated.connect([this](uint32_t fps) {
        if (m_session) {
            m_session->fpsUpdated.fire(fps);
        }
    });

    // 帧就绪信号 - 只通知，不消费帧
    // 渲染端收到信号后自己调用 session->consumeFrame() 和 releaseFrame()
    m_streamManager->frameReady.connect([this]() {
        if (m_session) {
            m_session->frameAvailable.fire();
        }
    });

    m_streamManager->streamStopped.connect([this]() {
        LOGD() << "[DeviceController] Stream stopped";
        if (m_server) {
            m_server->stop();
        }
    });

    // 创建 Server
    m_server = new Server();
    m_server->serverStarted.connect([this](bool ok, const std::string& name, const Size& sz) {
        auto token = m_aliveToken;
        dispatch::postToMain([this, token, ok, name, sz]() {
            if (!token || !token->load(std::memory_order_acquire)) return;
            onServerStart(ok, name, sz);
        });
    });
    m_server->serverStoped.connect([this]() {
        auto token = m_aliveToken;
        dispatch::postToMain([this, token]() {
            if (!token || !token->load(std::memory_order_acquire)) return;
            onServerStop();
        });
    });

    LOG_I("[DeviceController] Created for %s", params.serial.c_str());
}

DeviceController::~DeviceController()
{
    if (m_aliveToken) {
        m_aliveToken->store(false, std::memory_order_release);
    }
    stop();
}

bool DeviceController::start()
{
    if (!m_server) {
        return false;
    }
    m_stopping = false;

    // 转换参数为 Server::ServerParams
    Server::ServerParams serverParams;
    serverParams.serial = m_params.serial;
    serverParams.serverLocalPath = m_params.serverLocalPath;
    serverParams.serverRemotePath = m_params.serverRemotePath;
    serverParams.maxSize = m_params.maxSize;
    serverParams.bitRate = m_params.bitRate;
    serverParams.maxFps = m_params.maxFps;
    serverParams.captureOrientationLock = m_params.captureOrientationLock;
    serverParams.captureOrientation = m_params.captureOrientation;
    serverParams.stayAwake = m_params.stayAwake;
    serverParams.serverVersion = m_params.serverVersion;
    serverParams.logLevel = m_params.logLevel;
    serverParams.codecOptions = m_params.codecOptions;
    serverParams.codecName = m_params.codecName;
    serverParams.videoCodec = m_params.videoCodec;
    serverParams.localPort = m_params.localPort;
    serverParams.localPortAudio = m_params.localPortAudio;
    serverParams.localPortCtrl = m_params.localPortCtrl;
    serverParams.useReverse = m_params.useReverse;
    serverParams.kcpPort = m_params.kcpPort;
    serverParams.scid = m_params.scid;

    // 从配置读取通道启用状态，传递给 Server → TcpServerHandler/KcpServer
    bool controlEnabled = ConfigCenter::instance().get<bool>("user/controlChannelEnabled", true);
    bool audioEnabled   = ConfigCenter::instance().get<bool>("user/audioChannelEnabled", false);  // 默认关闭
    bool auxEnabled     = ConfigCenter::instance().get<bool>("user/auxChannelEnabled", true);

    serverParams.control      = controlEnabled;
    serverParams.audioEnabled = audioEnabled;
    serverParams.auxEnabled   = auxEnabled;

    return m_server->start(serverParams);
}

void DeviceController::stop()
{
    if (m_stopping) {
        return;
    }
    m_stopping = true;

    // 先终止异步 adb 进程，防止回调在已停止的控制器上触发
    if (m_adbSizeProcess) {
        m_adbSizeProcess->kill();
    }

    if (m_session) {
        m_session->stop();
    }
    if (m_audioManager) {
        m_audioManager->stopStream();
        delete m_audioManager;
        m_audioManager = nullptr;
    }
    if (m_streamManager) {
        m_streamManager->stop();
    }
    if (m_server) {
        m_server->stop();
        delete m_server;
        m_server = nullptr;
    }
    delete m_auxChannel;
    m_auxChannel = nullptr;
}

bool DeviceController::isReversePort(uint16_t port) const
{
    return m_server && m_server->isReverse() && m_params.localPort == port;
}

void DeviceController::onServerStart(bool success, const std::string& deviceName, const Size& size)
{
    // 防止在已失败/已停止后再次处理（Server 可能多次回调）
    if (m_stopping) {
        LOGD() << "[DeviceController] Ignoring server callback (already stopping)";
        return;
    }

    if (!success) {
        LOGW() << "[DeviceController] Server start failed";
        // 先清理资源（stop 会自行设置 m_stopping），再通知 UI
        stop();
        connected.fire(false, m_params.serial, std::string(), Size());
        return;
    }

    LOGI() << "[DeviceController] Server started, size:" << size.width << "x" << size.height;
    m_mobileSize = size;

    // 配置流管线 (视频通道)
    bool videoEnabled = ConfigCenter::instance().get<bool>("user/videoChannelEnabled", true);
    if (videoEnabled) {
    if (size.isValid()) {
        m_streamManager->setFrameSize(size);
    } else {
        m_streamManager->setFrameSize(Size(m_params.maxSize, m_params.maxSize));
    }

    // 设置视频编解码器
    m_streamManager->setVideoCodec(m_params.videoCodec);

    // 安装 socket
    if (m_server->isWiFiMode()) {
        auto* kcpSocket = m_server->removeKcpVideoSocket();
        if (kcpSocket) {
            m_streamManager->installKcpVideoSocket(kcpSocket);
            LOGI() << "[DeviceController] Installed KCP video socket";
        }
    } else {
        auto* tcpSocket = m_server->removeVideoSocket();
        if (tcpSocket) {
            m_streamManager->installVideoSocket(tcpSocket);
            LOGI() << "[DeviceController] Installed TCP video socket";
        }
    }

    // 启动流管线
    if (!m_streamManager->start()) {
        LOGW() << "[DeviceController] Failed to start stream manager";
    }
    } else {
        LOGI() << "[DeviceController] Video channel disabled by settings";
    }

    // 配置 InputManager (控制通道)
    bool controlEnabled = ConfigCenter::instance().get<bool>("user/controlChannelEnabled", true);
    if (controlEnabled && m_session && m_session->inputManager()) {
        auto* inputMgr = m_session->inputManager();

        auto sendCallback = [this](const char* data, int len) -> int64_t {
            if (m_server->isWiFiMode() && m_server->getKcpControlSocket()) {
                return m_server->getKcpControlSocket()->write(data, len);
            } else if (m_server->getControlSocket()) {
                return m_server->getControlSocket()->send(data, len);
            }
            return -1;
        };

        inputMgr->initialize(sendCallback, m_params.gameScript);

        if (m_server->isWiFiMode()) {
            inputMgr->setKcpControlSocket(m_server->getKcpControlSocket());
        } else {
            inputMgr->setTcpControlSocket(m_server->getControlSocket());
        }

        inputMgr->start();
        LOGI() << "[DeviceController] InputManager started";

        // 异步获取手机分辨率
        if (m_adbSizeProcess) {
            std::vector<std::string> args = {"shell", "wm", "size"};
            m_adbSizeProcess->execute(m_params.serial, args);
        }
    } else if (!controlEnabled) {
        LOGI() << "[DeviceController] Control channel disabled by settings";
    }

    // 配置辅助通道 (独立于控制通道，USB 和 WiFi 模式都使用 TCP)
    bool auxEnabled = ConfigCenter::instance().get<bool>("user/auxChannelEnabled", true);
    if (auxEnabled) {
        m_auxChannel = new AuxChannelClient();
        auto* auxSocket = m_server->getAuxTcpSocket();
        if (auxSocket) {
            m_auxChannel->setTcpSocket(auxSocket);
            LOGI() << "[DeviceController] AuxChannel configured TCP";
        }
        if (m_session) {
            m_session->setAuxChannel(m_auxChannel);
        }
    }

    // 配置音频通道 (仅在启用时创建 AudioStreamManager)
    bool audioEnabled = ConfigCenter::instance().get<bool>("user/audioChannelEnabled", false);
    if (audioEnabled) {
        auto* audioSocket = m_server->getAudioTcpSocket();
        if (audioSocket) {
            m_audioManager = new AudioStreamManager();
            m_audioManager->installSocket(audioSocket);
            m_audioManager->setMuted(false); // 音频已启用，立即播放
            m_audioManager->startStream();
            LOGI() << "[DeviceController] AudioStreamManager started (unmuted)";

            // 将 AudioStreamManager 暴露给 DeviceSession
            if (m_session) {
                m_session->setAudioManager(m_audioManager);
            }
        }
    } else {
        LOGI() << "[DeviceController] Audio channel disabled by settings";
    }

    LOGI() << "[DeviceController] About to fire connected signal";
    connected.fire(true, m_params.serial, deviceName, size);
    LOGI() << "[DeviceController] connected.fire() done";
}

void DeviceController::onServerStop()
{
    LOGD() << "[DeviceController] Server stopped";
    std::string serial = m_params.serial;
    stop();
    disconnected.fire(serial);
}

void DeviceController::onAdbSizeResult(AdbProcess::ADB_EXEC_RESULT processResult)
{
    // 防止在已停止/销毁过程中处理回调
    if (m_stopping) {
        LOGD() << "[DeviceController] Ignoring adb size result (already stopping)";
        return;
    }

    if (AdbProcess::AER_SUCCESS_EXEC != processResult) {
        LOGW() << "[DeviceController] ADB wm size failed";
        return;
    }

    std::string output = m_adbSizeProcess->getStdOut();
    if (output.empty()) return;

    std::regex regexOverride("Override size:\\s*(\\d+)x(\\d+)");
    std::regex regexPhysical("Physical size:\\s*(\\d+)x(\\d+)");

    std::smatch match;
    bool matched = std::regex_search(output, match, regexOverride);
    if (!matched) {
        matched = std::regex_search(output, match, regexPhysical);
    }

    if (matched && match.size() >= 3) {
        int w = std::stoi(match[1].str());
        int h = std::stoi(match[2].str());
        if (w > 0 && h > 0) {
            m_mobileSize = Size(w, h);
            LOGD() << "[DeviceController] Got mobile size:" << m_mobileSize.width << "x" << m_mobileSize.height;

            if (m_session && m_session->inputManager()) {
                m_session->inputManager()->setMobileSize(m_mobileSize);
            }
        }
    }
}

// ============================================================================
// DeviceManage 实现
// ============================================================================

IDeviceManage& IDeviceManage::getInstance() {
    static DeviceManage dm;
    return dm;
}

DeviceManage::DeviceManage() {
    Demuxer::init();
}

DeviceManage::~DeviceManage() {
    disconnectAllDevice();
    Demuxer::deInit();
}

core::DeviceSession* DeviceManage::getSession(const std::string &serial)
{
    auto it = m_devices.find(serial);
    if (it == m_devices.end() || !it->second) {
        return nullptr;
    }
    return it->second->session();
}

bool DeviceManage::connectDevice(DeviceParams params)
{
    if (params.serial.empty()) {
        return false;
    }
    if (m_devices.count(params.serial)) {
        // 已有旧实例（可能是上次断开后的残留），先清理
        auto* old = m_devices[params.serial];
        m_devices.erase(params.serial);
        if (old) {
            old->connected.disconnectAll();
            old->disconnected.disconnectAll();
            old->stop();
            delete old;
        }
    }
    if (DM_MAX_DEVICES_NUM < static_cast<int>(m_devices.size())) {
        LOG_I("over the maximum number of connections");
        return false;
    }

    // 创建设备控制器
    auto* controller = new DeviceController(params);
    controller->connected.connect(
        [this](bool success, const std::string& serial, const std::string& deviceName, const Size& size) {
            onDeviceConnected(success, serial, deviceName, size);
        });
    controller->disconnected.connect(
        [this](const std::string& serial) {
            onDeviceDisconnected(serial);
        });

    if (!controller->start()) {
        delete controller;
        return false;
    }

    m_devices[params.serial] = controller;
    return true;
}

bool DeviceManage::disconnectDevice(const std::string &serial)
{
    if (serial.empty() || !m_devices.count(serial)) {
        return false;
    }

    auto it = m_devices.find(serial);
    auto* controller = it->second;
    m_devices.erase(it);
    if (controller) {
        // 断开所有信号连接，防止 stop() 触发 onServerStop() → onDeviceDisconnected() 竞态
        controller->connected.disconnectAll();
        controller->disconnected.disconnectAll();
        controller->stop();
        delete controller;
        return true;
    }
    return false;
}

void DeviceManage::disconnectAllDevice()
{
    // 移走 map 避免迭代器失效 (stop() 可能通过 postToMain 触发 removeDevice)
    auto devices = std::move(m_devices);
    for (auto& [key, controller] : devices) {
        if (controller) {
            controller->connected.disconnectAll();
            controller->disconnected.disconnectAll();
            controller->stop();
            delete controller;
        }
    }
}

void DeviceManage::onDeviceConnected(bool success, const std::string &serial, const std::string &deviceName, const Size &size)
{
    notifyDeviceConnected(success, serial, deviceName, size);
    if (!success) {
        removeDevice(serial);
    }
}

void DeviceManage::onDeviceDisconnected(const std::string& serial)
{
    notifyDeviceDisconnected(serial);
    removeDevice(serial);
}

// === 回调管理 ===

int DeviceManage::addDeviceConnectedListener(DeviceConnectedCb cb)
{
    int id = m_nextListenerId++;
    m_connectedListeners.emplace_back(id, std::move(cb));
    return id;
}

int DeviceManage::addDeviceDisconnectedListener(DeviceDisconnectedCb cb)
{
    int id = m_nextListenerId++;
    m_disconnectedListeners.emplace_back(id, std::move(cb));
    return id;
}

void DeviceManage::removeDeviceListener(int id)
{
    m_connectedListeners.erase(
        std::remove_if(m_connectedListeners.begin(), m_connectedListeners.end(),
                       [id](const auto& p) { return p.first == id; }),
        m_connectedListeners.end());
    m_disconnectedListeners.erase(
        std::remove_if(m_disconnectedListeners.begin(), m_disconnectedListeners.end(),
                       [id](const auto& p) { return p.first == id; }),
        m_disconnectedListeners.end());
}

void DeviceManage::notifyDeviceConnected(bool success, const std::string& serial, const std::string& deviceName, const Size& size)
{
    for (auto& [lid, cb] : m_connectedListeners) {
        cb(success, serial, deviceName, size);
    }
}

void DeviceManage::notifyDeviceDisconnected(const std::string& serial)
{
    for (auto& [lid, cb] : m_disconnectedListeners) {
        cb(serial);
    }
}

uint16_t DeviceManage::getFreePort()
{
    uint16_t port = m_localPortStart;
    while (port < m_localPortStart + DM_MAX_DEVICES_NUM) {
        bool used = false;
        for (auto& [key, controller] : m_devices) {
            if (controller && controller->isReversePort(port)) {
                used = true;
                break;
            }
        }
        if (!used) {
            return port;
        }
        port++;
    }
    return 0;
}

void DeviceManage::removeDevice(const std::string &serial)
{
    if (!serial.empty() && m_devices.count(serial)) {
        auto it = m_devices.find(serial);
        auto* controller = it->second;
        m_devices.erase(it);
        if (controller) {
            controller->connected.disconnectAll();
            controller->disconnected.disconnectAll();
            controller->stop();
            delete controller;
        }
    }
}

}
