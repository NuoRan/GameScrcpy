#include "ConnectionManager.h"
#include "server.h"
#include "videosocket.h"
#include "kcpvideosocket.h"
#include "kcpcontrolsocket.h"
#include "StringUtils.h"
#include <algorithm>

#define LOG_TAG "ConnectionMgr"
#include "Logger.h"

namespace qsc {
namespace core {

ConnectionManager::ConnectionManager()
{
    LOG_I("[ConnectionManager] Created");
}

ConnectionManager::~ConnectionManager()
{
    disconnectDevice();
    LOG_I("[ConnectionManager] Destroyed");
}

bool ConnectionManager::connectDevice(const std::string& serial,
                                      uint16_t localPort,
                                      int maxWidth,
                                      int maxHeight,
                                      uint32_t bitRate,
                                      uint32_t maxFps)
{
    if (m_state == ConnectionState::Connecting || m_state == ConnectionState::Connected) {
        LOG_W("[ConnectionManager] Already connecting or connected");
        return false;
    }

    m_serial = serial;
    setState(ConnectionState::Connecting);

    // 创建 Server
    if (m_server) {
        m_server->stop();
        delete m_server;
    }
    m_server = new Server();

    // 连接 Server 信号 (Signal<>)
    m_server->serverStarted.connect(
        [this](bool ok, const std::string& name, const Size& sz) {
            onServerStarted(ok, name, sz);
        });
    m_server->serverStoped.connect(
        [this]() {
            onServerStopped();
        });

    // 设置 Server 参数
    Server::ServerParams params;
    params.serial = serial;
    params.localPort = localPort;
    params.maxSize = static_cast<uint16_t>(std::max(maxWidth, maxHeight));
    params.bitRate = bitRate;
    params.maxFps = maxFps;
    params.codecOptions = "";
    params.codecName = "";

    // 启动 Server
    if (!m_server->start(params)) {
        LOG_W("[ConnectionManager] Failed to start server for %s", serial.c_str());
        setState(ConnectionState::Error);
        error.fire(std::string("Failed to start server"));
        return false;
    }

    LOG_I("[ConnectionManager] Connecting to %s", serial.c_str());
    return true;
}

void ConnectionManager::disconnectDevice()
{
    if (m_state == ConnectionState::Disconnected) {
        return;
    }

    LOG_I("[ConnectionManager] Disconnecting from %s", m_serial.c_str());

    cleanup();
    setState(ConnectionState::Disconnected);
    disconnected.fire();
}

void ConnectionManager::setState(ConnectionState state)
{
    if (m_state != state) {
        m_state = state;
        stateChanged.fire(state);
    }
}

void ConnectionManager::cleanup()
{
    if (m_server) {
        m_server->stop();
        delete m_server;
        m_server = nullptr;
    }

    m_videoSocket = nullptr;
    m_kcpVideoSocket = nullptr;
    m_kcpControlSocket = nullptr;
    m_frameSize = Size();
}

void ConnectionManager::onServerStarted(bool success, const std::string& deviceName, const Size& size)
{
    (void)deviceName;

    if (!success) {
        LOG_W("[ConnectionManager] Server start failed");
        setState(ConnectionState::Error);
        error.fire(std::string("Server start failed"));
        return;
    }

    m_frameSize = size;
    m_useKcp = m_server->isWiFiMode();

    LOG_I("[ConnectionManager] Server started, size: %dx%d, KCP: %s",
          size.width, size.height, m_useKcp ? "yes" : "no");

    // 获取 Sockets
    if (m_useKcp) {
        // KCP 模式
        m_kcpVideoSocket = m_server->removeKcpVideoSocket();
        m_kcpControlSocket = m_server->getKcpControlSocket();

        if (m_kcpVideoSocket) {
            setState(ConnectionState::Connected);
            kcpVideoSocketReady.fire(m_kcpVideoSocket);
            if (m_kcpControlSocket) {
                kcpControlSocketReady.fire(m_kcpControlSocket);
            }
            connected.fire(m_frameSize);
        } else {
            LOG_W("[ConnectionManager] Failed to get KCP video socket");
            setState(ConnectionState::Error);
            error.fire(std::string("Failed to get video socket"));
        }
    } else {
        // TCP 模式
        m_videoSocket = m_server->removeVideoSocket();

        if (m_videoSocket) {
            setState(ConnectionState::Connected);
            videoSocketReady.fire(m_videoSocket);
            connected.fire(m_frameSize);
        } else {
            LOG_W("[ConnectionManager] Failed to get TCP video socket");
            setState(ConnectionState::Error);
            error.fire(std::string("Failed to get video socket"));
        }
    }
}

void ConnectionManager::onServerStopped()
{
    LOG_I("[ConnectionManager] Server stopped");

    if (m_state != ConnectionState::Disconnected) {
        cleanup();
        setState(ConnectionState::Disconnected);
        disconnected.fire();
    }
}

} // namespace core
} // namespace qsc
