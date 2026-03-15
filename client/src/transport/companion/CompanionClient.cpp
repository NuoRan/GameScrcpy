#include "CompanionClient.h"
#include <QtEndian>
#include <QNetworkProxy>
#include <cstring>

CompanionClient::CompanionClient(QObject* parent) : QObject(parent) {}

CompanionClient::~CompanionClient() { disconnectFromDevice(); }

void CompanionClient::connectToDevice(const QString& ip, quint16 port)
{
    disconnectFromDevice();
    m_deviceIp = ip;
    m_socket = new QTcpSocket(this);
    m_socket->setProxy(QNetworkProxy::NoProxy);
    connect(m_socket, &QTcpSocket::connected, this, &CompanionClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &CompanionClient::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &CompanionClient::onError);
    m_socket->connectToHost(ip, port);
}

void CompanionClient::disconnectFromDevice()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_screenshotSocket) {
        m_screenshotSocket->abort();
        m_screenshotSocket->deleteLater();
        m_screenshotSocket = nullptr;
    }
    m_screenshotBuf.clear();
    m_screenshotExpectedLen = -1;
}

bool CompanionClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void CompanionClient::sendCursorPos(float nx, float ny)
{
    if (!isConnected()) return;
    char buf[9];
    buf[0] = 0x01;
    quint32 bx, by;
    std::memcpy(&bx, &nx, 4);
    std::memcpy(&by, &ny, 4);
    bx = qToBigEndian(bx);
    by = qToBigEndian(by);
    std::memcpy(buf + 1, &bx, 4);
    std::memcpy(buf + 5, &by, 4);
    m_socket->write(buf, 9);
}

void CompanionClient::cleanupScreenshotSocket()
{
    if (!m_screenshotSocket) return;
    m_screenshotSocket->disconnect(); // 断开所有信号
    m_screenshotSocket->abort();
    m_screenshotSocket->deleteLater();
    m_screenshotSocket = nullptr;
    m_screenshotBuf.clear();
    m_screenshotExpectedLen = -1;
}

void CompanionClient::requestScreenshot()
{
    if (m_deviceIp.isEmpty()) return;
    // 清理上一次的截屏连接（如果有）
    cleanupScreenshotSocket();

    m_screenshotSocket = new QTcpSocket(this);
    m_screenshotSocket->setProxy(QNetworkProxy::NoProxy);
    m_screenshotBuf.clear();
    m_screenshotExpectedLen = -1;

    connect(m_screenshotSocket, &QTcpSocket::connected, this, [this]() {
        if (!m_screenshotSocket) return;
        char buf[1] = {0x02};
        m_screenshotSocket->write(buf, 1);
        m_screenshotSocket->flush();
    });

    connect(m_screenshotSocket, &QTcpSocket::readyRead, this, [this]() {
        handleScreenshotData();
    });

    connect(m_screenshotSocket, &QTcpSocket::disconnected, this, [this]() {
        handleScreenshotData();
        cleanupScreenshotSocket();
    });

    connect(m_screenshotSocket, &QTcpSocket::errorOccurred, this, [this]() {
        if (m_screenshotSocket)
            emit errorOccurred(m_screenshotSocket->errorString());
        cleanupScreenshotSocket();
    });

    m_screenshotSocket->connectToHost(m_deviceIp, SCREENSHOT_PORT);
}

void CompanionClient::hideCursor()
{
    if (!isConnected()) return;
    char buf[1] = {0x03};
    m_socket->write(buf, 1);
}

void CompanionClient::onConnected()
{
    emit connected();
}

void CompanionClient::onDisconnected()
{
    emit disconnected();
}

void CompanionClient::onError(QAbstractSocket::SocketError)
{
    if (m_socket)
        emit errorOccurred(m_socket->errorString());
}

void CompanionClient::handleScreenshotData()
{
    if (!m_screenshotSocket) return;
    m_screenshotBuf.append(m_screenshotSocket->readAll());

    while (!m_screenshotBuf.isEmpty()) {
        if (m_screenshotExpectedLen < 0) {
            if (m_screenshotBuf.size() < 5) return;
            if (static_cast<uint8_t>(m_screenshotBuf[0]) != 0x82) {
                m_screenshotBuf.clear();
                return;
            }
            quint32 len;
            std::memcpy(&len, m_screenshotBuf.constData() + 1, 4);
            len = qFromBigEndian(len);
            m_screenshotExpectedLen = static_cast<int>(len);
            m_screenshotBuf.remove(0, 5);
        }

        if (m_screenshotExpectedLen == 0) {
            m_screenshotExpectedLen = -1;
            emit screenshotFailed();
            cleanupScreenshotSocket();
            return;
        }

        if (m_screenshotBuf.size() < m_screenshotExpectedLen) return;

        QByteArray jpeg = m_screenshotBuf.left(m_screenshotExpectedLen);
        m_screenshotBuf.remove(0, m_screenshotExpectedLen);
        m_screenshotExpectedLen = -1;

        QImage img;
        if (img.loadFromData(jpeg, "JPEG")) {
            emit screenshotReceived(img);
        }

        // 截屏完成，清理连接（服务端也会关闭）
        cleanupScreenshotSocket();
        return;
    }
}
