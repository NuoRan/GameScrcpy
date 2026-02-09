#include <QDebug>
#include <QRegularExpression>

#include "devicemanage.h"
#include "demuxer.h"
#include "server.h"
#include "kcpvideosocket.h"
#include "kcpcontrolsocket.h"
#include "videosocket.h"
#include "adbprocess.h"

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

DeviceController::DeviceController(const DeviceParams& params, QObject* parent)
    : QObject(parent)
    , m_params(params)
{
    // 初始化 ADB 进程（用于获取手机实际分辨率）
    m_adbSizeProcess = new AdbProcess(this);
    connect(m_adbSizeProcess, &AdbProcess::adbProcessResult,
            this, &DeviceController::onAdbSizeResult);

    // 转换参数
    core::SessionParams sessionParams;
    sessionParams.serial = params.serial;
    sessionParams.serverLocalPath = params.serverLocalPath;
    sessionParams.maxSize = params.maxSize;
    sessionParams.bitRate = params.bitRate;
    sessionParams.maxFps = params.maxFps;
    sessionParams.useKcp = params.serial.contains(':');
    sessionParams.deviceIP = sessionParams.useKcp ? params.serial.split(':').first() : QString();
    sessionParams.kcpPort = params.kcpPort;
    sessionParams.tcpPort = params.localPort;
    sessionParams.useReverse = params.useReverse;
    sessionParams.serverRemotePath = params.serverRemotePath;
    sessionParams.serverVersion = params.serverVersion;
    sessionParams.logLevel = params.logLevel;
    sessionParams.codecOptions = params.codecOptions;
    sessionParams.codecName = params.codecName;
    sessionParams.closeScreen = params.closeScreen;
    sessionParams.keyMapJson = params.gameScript;
    sessionParams.frameSize = QSize(params.maxSize, params.maxSize);

    // 创建 DeviceSession
    m_session = std::make_unique<core::DeviceSession>(sessionParams, this);

    // 创建零拷贝视频管线
    m_streamManager = std::make_unique<core::ZeroCopyStreamManager>(this);

    // 【零拷贝优化】将 FrameQueue 传递给 DeviceSession，让渲染端直接消费
    m_session->setFrameQueue(m_streamManager->frameQueue());

    // 连接流管理器信号
    connect(m_streamManager.get(), &core::ZeroCopyStreamManager::fpsUpdated, this, [this](quint32 fps) {
        if (m_session) {
            emit m_session->fpsUpdated(fps);
        }
    });

    // 【零拷贝优化】帧就绪信号 - 只通知，不消费帧
    // 渲染端收到信号后自己调用 session->consumeFrame() 和 releaseFrame()
    connect(m_streamManager.get(), &core::ZeroCopyStreamManager::frameReady, this, [this]() {
        if (m_session) {
            emit m_session->frameAvailable();
        }
    }, Qt::DirectConnection);

    connect(m_streamManager.get(), &core::ZeroCopyStreamManager::streamStopped, this, [this]() {
        qDebug() << "[DeviceController] Stream stopped";
        stop();
        emit disconnected(m_params.serial);
    });

    // 创建 Server
    m_server = new Server(this);
    connect(m_server, &Server::serverStarted, this, &DeviceController::onServerStart);
    connect(m_server, &Server::serverStoped, this, &DeviceController::onServerStop);

    qInfo("[DeviceController] Created for %s", qPrintable(params.serial));
}

DeviceController::~DeviceController()
{
    stop();
}

bool DeviceController::start()
{
    if (!m_server) {
        return false;
    }

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
    serverParams.localPort = m_params.localPort;
    serverParams.localPortCtrl = m_params.localPortCtrl;
    serverParams.useReverse = m_params.useReverse;
    serverParams.kcpPort = m_params.kcpPort;
    serverParams.scid = m_params.scid;

    return m_server->start(serverParams);
}

void DeviceController::stop()
{
    if (m_session) {
        m_session->stop();
    }
    if (m_streamManager) {
        m_streamManager->stop();
    }
    if (m_server) {
        m_server->stop();
    }
}

bool DeviceController::isReversePort(quint16 port) const
{
    return m_server && m_server->isReverse() && m_params.localPort == port;
}

void DeviceController::onServerStart(bool success, const QString& deviceName, const QSize& size)
{
    if (!success) {
        qWarning() << "[DeviceController] Server start failed";
        emit connected(false, m_params.serial, QString(), QSize());
        return;
    }

    qDebug() << "[DeviceController] Server started, size:" << size;
    m_mobileSize = size;

    // 配置流管线
    if (size.isValid()) {
        m_streamManager->setFrameSize(size);
    } else {
        m_streamManager->setFrameSize(QSize(m_params.maxSize, m_params.maxSize));
    }

    // 安装 socket
    if (m_server->isWiFiMode()) {
        auto* kcpSocket = m_server->removeKcpVideoSocket();
        if (kcpSocket) {
            m_streamManager->installKcpVideoSocket(kcpSocket);
            qDebug() << "[DeviceController] Installed KCP video socket";
        }
    } else {
        auto* tcpSocket = m_server->removeVideoSocket();
        if (tcpSocket) {
            m_streamManager->installVideoSocket(tcpSocket);
            qDebug() << "[DeviceController] Installed TCP video socket";
        }
    }

    // 启动流管线
    if (!m_streamManager->start()) {
        qWarning() << "[DeviceController] Failed to start stream manager";
    }

    // 配置 InputManager
    if (m_session && m_session->inputManager()) {
        auto* inputMgr = m_session->inputManager();

        auto sendCallback = [this](const QByteArray& data) -> qint64 {
            if (m_server->isWiFiMode() && m_server->getKcpControlSocket()) {
                return m_server->getKcpControlSocket()->write(data);
            } else if (m_server->getControlSocket()) {
                return m_server->getControlSocket()->write(data);
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
        qDebug() << "[DeviceController] InputManager started";

        // 异步获取手机分辨率
        if (m_adbSizeProcess) {
            QStringList args;
            args << "shell" << "wm" << "size";
            m_adbSizeProcess->execute(m_params.serial, args);
        }
    }

    emit connected(true, m_params.serial, deviceName, size);
}

void DeviceController::onServerStop()
{
    qDebug() << "[DeviceController] Server stopped";
    QString serial = m_params.serial;
    stop();
    emit disconnected(serial);
}

void DeviceController::onAdbSizeResult(AdbProcess::ADB_EXEC_RESULT processResult)
{
    if (AdbProcess::AER_SUCCESS_EXEC != processResult) {
        qWarning() << "[DeviceController] ADB wm size failed";
        return;
    }

    QString output = m_adbSizeProcess->getStdOut();
    if (output.isEmpty()) return;

    QRegularExpression regexOverride("Override size:\\s*(\\d+)x(\\d+)");
    QRegularExpression regexPhysical("Physical size:\\s*(\\d+)x(\\d+)");

    QRegularExpressionMatch match = regexOverride.match(output);
    if (!match.hasMatch()) {
        match = regexPhysical.match(output);
    }

    if (match.hasMatch()) {
        int w = match.captured(1).toInt();
        int h = match.captured(2).toInt();
        if (w > 0 && h > 0) {
            m_mobileSize = QSize(w, h);
            qDebug() << "[DeviceController] Got mobile size:" << m_mobileSize;

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

core::DeviceSession* DeviceManage::getSession(const QString &serial)
{
    auto it = m_devices.find(serial);
    if (it == m_devices.end() || !it.value()) {
        return nullptr;
    }
    return it.value()->session();
}

bool DeviceManage::connectDevice(DeviceParams params)
{
    if (params.serial.trimmed().isEmpty()) {
        return false;
    }
    if (m_devices.contains(params.serial)) {
        return false;
    }
    if (DM_MAX_DEVICES_NUM < m_devices.size()) {
        qInfo("over the maximum number of connections");
        return false;
    }

    // 创建设备控制器
    auto* controller = new DeviceController(params, this);
    connect(controller, &DeviceController::connected, this, &DeviceManage::onDeviceConnected);
    connect(controller, &DeviceController::disconnected, this, &DeviceManage::onDeviceDisconnected);

    if (!controller->start()) {
        delete controller;
        return false;
    }

    m_devices[params.serial] = controller;
    return true;
}

bool DeviceManage::disconnectDevice(const QString &serial)
{
    if (serial.isEmpty() || !m_devices.contains(serial)) {
        return false;
    }

    auto* controller = m_devices.take(serial);
    if (controller) {
        controller->stop();
        controller->deleteLater();
        return true;
    }
    return false;
}

void DeviceManage::disconnectAllDevice()
{
    for (auto* controller : m_devices) {
        if (controller) {
            controller->stop();
            controller->deleteLater();
        }
    }
    m_devices.clear();
}

void DeviceManage::onDeviceConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size)
{
    emit deviceConnected(success, serial, deviceName, size);
    if (!success) {
        removeDevice(serial);
    }
}

void DeviceManage::onDeviceDisconnected(const QString& serial)
{
    emit deviceDisconnected(serial);
    removeDevice(serial);
}

quint16 DeviceManage::getFreePort()
{
    quint16 port = m_localPortStart;
    while (port < m_localPortStart + DM_MAX_DEVICES_NUM) {
        bool used = false;
        for (auto* controller : m_devices) {
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

void DeviceManage::removeDevice(const QString &serial)
{
    if (!serial.isEmpty() && m_devices.contains(serial)) {
        auto* controller = m_devices.take(serial);
        if (controller) {
            controller->deleteLater();
        }
    }
}

}
