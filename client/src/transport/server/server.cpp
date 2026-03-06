#define LOG_TAG "Server"
#include "Logger.h"

#include "server.h"
#include "kcpserver.h"
#include "kcpvideosocket.h"
#include "kcpcontrolsocket.h"
#include "tcpserverhandler.h"
#include "videosocket.h"

Server::Server()
{
}

Server::~Server()
{
    stop();
}

bool Server::start(Server::ServerParams params)
{
    m_params = params;

    // 自动检测连接模式
    // 包含 ':' 的是 WiFi 连接 (如 192.168.1.100:5555)
    // 否则是 USB 连接 (如 abcd1234)
    m_useKcp = m_params.serial.find(':') != std::string::npos;

    if (m_useKcp) {
        LOGI() << "Server: Detected WiFi connection, using KCP mode";

        // 创建 KcpServer
        m_kcpServer = new KcpServer();
        // 桥接 KcpServer 的 Signal<> 到 Server 的 Signal<>
        m_kcpServer->serverStarted.connect(
            [this](bool ok, const std::string& name, const Size& sz) {
                serverStarted.fire(ok, name, sz);
            });
        m_kcpServer->serverStoped.connect(
            [this]() {
                serverStoped.fire();
            });

        // 转换参数
        KcpServer::ServerParams kcpParams;
        kcpParams.serial = m_params.serial;
        kcpParams.serverLocalPath = m_params.serverLocalPath;
        kcpParams.serverRemotePath = m_params.serverRemotePath;
        kcpParams.maxSize = m_params.maxSize;
        kcpParams.bitRate = m_params.bitRate;
        kcpParams.maxFps = m_params.maxFps;
        kcpParams.captureOrientationLock = m_params.captureOrientationLock;
        kcpParams.captureOrientation = m_params.captureOrientation;
        kcpParams.stayAwake = m_params.stayAwake;
        kcpParams.serverVersion = m_params.serverVersion;
        kcpParams.logLevel = m_params.logLevel;
        kcpParams.codecOptions = m_params.codecOptions;
        kcpParams.codecName = m_params.codecName;
        kcpParams.videoCodec = m_params.videoCodec;
        kcpParams.crop = m_params.crop;
        kcpParams.control = m_params.control;
        kcpParams.audioEnabled = m_params.audioEnabled;
        kcpParams.auxEnabled = m_params.auxEnabled;
        kcpParams.kcpPort = m_params.kcpPort;
        kcpParams.scid = m_params.scid;

        return m_kcpServer->start(kcpParams);
    } else {
        LOGI() << "Server: Detected USB connection, using TCP mode";

        // 创建 TcpServerHandler
        m_tcpServer = new TcpServerHandler();
        // 桥接 TcpServerHandler 的 Signal<> 到 Server 的 Signal<>
        m_tcpServer->serverStarted.connect(
            [this](bool ok, const std::string& name, const Size& sz) {
                serverStarted.fire(ok, name, sz);
            });
        m_tcpServer->serverStoped.connect(
            [this]() {
                serverStoped.fire();
            });

        // 转换参数
        TcpServerHandler::ServerParams tcpParams;
        tcpParams.serial = m_params.serial;
        tcpParams.serverLocalPath = m_params.serverLocalPath;
        tcpParams.serverRemotePath = m_params.serverRemotePath;
        tcpParams.localPort = m_params.localPort;
        tcpParams.localPortAudio = m_params.localPortAudio;
        tcpParams.localPortCtrl = m_params.localPortCtrl;
        tcpParams.localPortAux = m_params.localPortAux;
        tcpParams.maxSize = m_params.maxSize;
        tcpParams.bitRate = m_params.bitRate;
        tcpParams.maxFps = m_params.maxFps;
        tcpParams.useReverse = m_params.useReverse;
        tcpParams.captureOrientationLock = m_params.captureOrientationLock;
        tcpParams.captureOrientation = m_params.captureOrientation;
        tcpParams.stayAwake = m_params.stayAwake;
        tcpParams.serverVersion = m_params.serverVersion;
        tcpParams.logLevel = m_params.logLevel;
        tcpParams.codecOptions = m_params.codecOptions;
        tcpParams.codecName = m_params.codecName;
        tcpParams.videoCodec = m_params.videoCodec;
        tcpParams.crop = m_params.crop;
        tcpParams.control = m_params.control;
        tcpParams.audioEnabled = m_params.audioEnabled;
        tcpParams.auxEnabled = m_params.auxEnabled;
        tcpParams.scid = m_params.scid;

        return m_tcpServer->start(tcpParams);
    }
}

void Server::stop()
{
    if (m_kcpServer) {
        m_kcpServer->stop();
        delete m_kcpServer;
        m_kcpServer = nullptr;
    }
    if (m_tcpServer) {
        m_tcpServer->stop();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }
}

Server::ServerParams Server::getParams()
{
    return m_params;
}

bool Server::isReverse() const
{
    if (m_tcpServer) {
        return m_tcpServer->isReverse();
    }
    return false;
}

KcpVideoSocket* Server::removeKcpVideoSocket()
{
    if (m_kcpServer) {
        return m_kcpServer->removeKcpVideoSocket();
    }
    return nullptr;
}

KcpControlSocket* Server::getKcpControlSocket()
{
    if (m_kcpServer) {
        return m_kcpServer->getKcpControlSocket();
    }
    return nullptr;
}

VideoSocket* Server::removeVideoSocket()
{
    if (m_tcpServer) {
        return m_tcpServer->removeVideoSocket();
    }
    return nullptr;
}

NativeTcpSocket* Server::getControlSocket()
{
    if (m_tcpServer) {
        return m_tcpServer->getControlSocket();
    }
    return nullptr;
}

NativeTcpSocket* Server::getAudioTcpSocket()
{
    if (m_tcpServer) {
        return m_tcpServer->getAudioSocket();
    }
    if (m_kcpServer) {
        return m_kcpServer->getAudioTcpSocket();
    }
    return nullptr;
}

NativeTcpSocket* Server::getAuxTcpSocket()
{
    if (m_tcpServer) {
        return m_tcpServer->getAuxSocket();
    }
    if (m_kcpServer) {
        return m_kcpServer->getAuxTcpSocket();
    }
    return nullptr;
}

std::string Server::getServerIp() const
{
    if (m_kcpServer) {
        return m_kcpServer->getServerIp();
    }
    return std::string();
}
