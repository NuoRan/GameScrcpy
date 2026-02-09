#include "ConnectionManager.h"
#include "server.h"
#include "videosocket.h"
#include "kcpvideosocket.h"
#include "kcpcontrolsocket.h"

#include <QDebug>

namespace qsc {
namespace core {

ConnectionManager::ConnectionManager(QObject* parent)
    : QObject(parent)
{
    qInfo("[ConnectionManager] Created");
}

ConnectionManager::~ConnectionManager()
{
    disconnectDevice();
    qInfo("[ConnectionManager] Destroyed");
}

bool ConnectionManager::connectDevice(const QString& serial,
                                      quint16 localPort,
                                      int maxWidth,
                                      int maxHeight,
                                      quint32 bitRate,
                                      quint32 maxFps)
{
    if (m_state == ConnectionState::Connecting || m_state == ConnectionState::Connected) {
        qWarning("[ConnectionManager] Already connecting or connected");
        return false;
    }

    m_serial = serial;
    setState(ConnectionState::Connecting);

    // 创建 Server
    if (m_server) {
        m_server->deleteLater();
    }
    m_server = new Server(this);

    // 连接 Server 信号
    connect(m_server, &Server::serverStarted,
            this, &ConnectionManager::onServerStarted);
    connect(m_server, &Server::serverStoped,
            this, &ConnectionManager::onServerStopped);

    // 设置 Server 参数
    Server::ServerParams params;
    params.serial = serial;
    params.localPort = localPort;
    params.maxSize = static_cast<quint16>(qMax(maxWidth, maxHeight));
    params.bitRate = bitRate;
    params.maxFps = maxFps;
    params.codecOptions = "";
    params.codecName = "";

    // 启动 Server
    if (!m_server->start(params)) {
        qWarning("[ConnectionManager] Failed to start server for %s", qPrintable(serial));
        setState(ConnectionState::Error);
        emit error(tr("Failed to start server"));
        return false;
    }

    qInfo("[ConnectionManager] Connecting to %s", qPrintable(serial));
    return true;
}

void ConnectionManager::disconnectDevice()
{
    if (m_state == ConnectionState::Disconnected) {
        return;
    }

    qInfo("[ConnectionManager] Disconnecting from %s", qPrintable(m_serial));

    cleanup();
    setState(ConnectionState::Disconnected);
    emit disconnected();
}

void ConnectionManager::setState(ConnectionState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void ConnectionManager::cleanup()
{
    if (m_server) {
        m_server->stop();
        m_server->deleteLater();
        m_server = nullptr;
    }

    m_videoSocket = nullptr;
    m_kcpVideoSocket = nullptr;
    m_kcpControlSocket = nullptr;
    m_frameSize = QSize();
}

void ConnectionManager::onServerStarted(bool success, const QString& deviceName, const QSize& size)
{
    Q_UNUSED(deviceName);

    if (!success) {
        qWarning("[ConnectionManager] Server start failed");
        setState(ConnectionState::Error);
        emit error(tr("Server start failed"));
        return;
    }

    m_frameSize = size;
    m_useKcp = m_server->isWiFiMode();

    qInfo("[ConnectionManager] Server started, size: %dx%d, KCP: %s",
          size.width(), size.height(), m_useKcp ? "yes" : "no");

    // 获取 Sockets
    if (m_useKcp) {
        // KCP 模式
        m_kcpVideoSocket = m_server->removeKcpVideoSocket();
        m_kcpControlSocket = m_server->getKcpControlSocket();

        if (m_kcpVideoSocket) {
            setState(ConnectionState::Connected);
            emit kcpVideoSocketReady(m_kcpVideoSocket);
            if (m_kcpControlSocket) {
                emit kcpControlSocketReady(m_kcpControlSocket);
            }
            emit connected(m_frameSize);
        } else {
            qWarning("[ConnectionManager] Failed to get KCP video socket");
            setState(ConnectionState::Error);
            emit error(tr("Failed to get video socket"));
        }
    } else {
        // TCP 模式
        m_videoSocket = m_server->removeVideoSocket();

        if (m_videoSocket) {
            setState(ConnectionState::Connected);
            emit videoSocketReady(m_videoSocket);
            emit connected(m_frameSize);
        } else {
            qWarning("[ConnectionManager] Failed to get TCP video socket");
            setState(ConnectionState::Error);
            emit error(tr("Failed to get video socket"));
        }
    }
}

void ConnectionManager::onServerStopped()
{
    qInfo("[ConnectionManager] Server stopped");

    if (m_state != ConnectionState::Disconnected) {
        cleanup();
        setState(ConnectionState::Disconnected);
        emit disconnected();
    }
}

} // namespace core
} // namespace qsc
