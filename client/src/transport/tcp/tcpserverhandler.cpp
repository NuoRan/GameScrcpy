#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <QTimerEvent>

#include "tcpserverhandler.h"

#define DEVICE_NAME_FIELD_LENGTH 64
#define SOCKET_NAME_PREFIX "scrcpy"
#define MAX_CONNECT_COUNT 30
#define MAX_RESTART_COUNT 1

static quint32 bufferRead32be(quint8 *buf)
{
    return static_cast<quint32>((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
}

TcpServerHandler::TcpServerHandler(QObject *parent) : QObject(parent)
{
    connect(&m_workProcess, &qsc::AdbProcess::adbProcessResult, this, &TcpServerHandler::onWorkProcessResult);
    connect(&m_serverProcess, &qsc::AdbProcess::adbProcessResult, this, &TcpServerHandler::onWorkProcessResult);

    // 处理 video socket 连接
    connect(&m_serverSocket, &QTcpServer::newConnection, this, [this]() {
        QTcpSocket *tmp = m_serverSocket.nextPendingConnection();
        VideoSocket *vs = dynamic_cast<VideoSocket *>(tmp);
        if (vs) {
            m_videoSocket = vs;
            if (!m_videoSocket->isValid() || !readInfo(m_videoSocket, m_deviceName, m_deviceSize)) {
                stop();
                emit serverStarted(false);
                return;
            }
            m_serverSocket.close();
            checkBothConnected();
        }
    });

    // 处理 control socket 连接
    connect(&m_serverSocketCtrl, &QTcpServer::newConnection, this, [this]() {
        QTcpSocket *tmp = m_serverSocketCtrl.nextPendingConnection();
        if (tmp && tmp->isValid()) {
            m_controlSocket = tmp;
            m_serverSocketCtrl.close();
            checkBothConnected();
        } else {
            stop();
            emit serverStarted(false);
        }
    });
}

TcpServerHandler::~TcpServerHandler() {}

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
    QString baseName = (m_params.scid == -1)
        ? QString(SOCKET_NAME_PREFIX)
        : QString(SOCKET_NAME_PREFIX "_%1").arg(m_params.scid, 8, 16, QChar('0'));
    m_workProcess.reverse(m_params.serial, baseName + "_video", m_params.localPort);
    return true;
}

bool TcpServerHandler::enableTunnelReverseCtrl()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    QString baseName = (m_params.scid == -1)
        ? QString(SOCKET_NAME_PREFIX)
        : QString(SOCKET_NAME_PREFIX "_%1").arg(m_params.scid, 8, 16, QChar('0'));
    m_workProcess.reverse(m_params.serial, baseName + "_control", m_params.localPortCtrl);
    return true;
}

bool TcpServerHandler::disableTunnelReverse()
{
    QString baseName = (m_params.scid == -1)
        ? QString(SOCKET_NAME_PREFIX)
        : QString(SOCKET_NAME_PREFIX "_%1").arg(m_params.scid, 8, 16, QChar('0'));

    qsc::AdbProcess *adb1 = new qsc::AdbProcess();
    if (adb1) {
        connect(adb1, &qsc::AdbProcess::adbProcessResult, adb1, [adb1](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                adb1->deleteLater();
            }
        });
        adb1->reverseRemove(m_params.serial, baseName + "_video");
    }

    qsc::AdbProcess *adb2 = new qsc::AdbProcess();
    if (adb2) {
        connect(adb2, &qsc::AdbProcess::adbProcessResult, adb2, [adb2](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                adb2->deleteLater();
            }
        });
        adb2->reverseRemove(m_params.serial, baseName + "_control");
    }
    return true;
}

bool TcpServerHandler::enableTunnelForward()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    QString baseName = (m_params.scid == -1)
        ? QString(SOCKET_NAME_PREFIX)
        : QString(SOCKET_NAME_PREFIX "_%1").arg(m_params.scid, 8, 16, QChar('0'));
    m_workProcess.forward(m_params.serial, m_params.localPort, baseName + "_video");
    return true;
}

bool TcpServerHandler::enableTunnelForwardCtrl()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    QString baseName = (m_params.scid == -1)
        ? QString(SOCKET_NAME_PREFIX)
        : QString(SOCKET_NAME_PREFIX "_%1").arg(m_params.scid, 8, 16, QChar('0'));
    m_workProcess.forward(m_params.serial, m_params.localPortCtrl, baseName + "_control");
    return true;
}

bool TcpServerHandler::disableTunnelForward()
{
    qsc::AdbProcess *adb1 = new qsc::AdbProcess();
    if (adb1) {
        connect(adb1, &qsc::AdbProcess::adbProcessResult, adb1, [adb1](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                adb1->deleteLater();
            }
        });
        adb1->forwardRemove(m_params.serial, m_params.localPort);
    }

    qsc::AdbProcess *adb2 = new qsc::AdbProcess();
    if (adb2) {
        connect(adb2, &qsc::AdbProcess::adbProcessResult, adb2, [adb2](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                adb2->deleteLater();
            }
        });
        adb2->forwardRemove(m_params.serial, m_params.localPortCtrl);
    }
    return true;
}

bool TcpServerHandler::execute()
{
    if (m_serverProcess.isRuning()) {
        m_serverProcess.kill();
    }
    QStringList args;
    args << "shell";
    args << QString("CLASSPATH=%1").arg(m_params.serverRemotePath);
    args << "app_process";

#ifdef SERVER_DEBUGGER
#define SERVER_DEBUGGER_PORT "5005"
    args <<
#ifdef SERVER_DEBUGGER_METHOD_NEW
        "-XjdwpProvider:internal -XjdwpOptions:transport=dt_socket,suspend=y,server=y,address="
#else
        "-agentlib:jdwp=transport=dt_socket,suspend=y,server=y,address="
#endif
        SERVER_DEBUGGER_PORT,
#endif

    args << "/";
    args << "com.genymobile.scrcpy.Server";
    args << m_params.serverVersion;

    args << QString("video_bit_rate=%1").arg(QString::number(m_params.bitRate));
    if (!m_params.logLevel.isEmpty()) {
        args << QString("log_level=%1").arg(m_params.logLevel);
    }
    if (m_params.maxSize > 0) {
        args << QString("max_size=%1").arg(QString::number(m_params.maxSize));
    }
    if (m_params.maxFps > 0) {
        args << QString("max_fps=%1").arg(QString::number(m_params.maxFps));
    }

    if (1 == m_params.captureOrientationLock) {
        args << QString("capture_orientation=@%1").arg(m_params.captureOrientation);
    } else if (2 == m_params.captureOrientationLock) {
        args << QString("capture_orientation=@");
    } else {
        args << QString("capture_orientation=%1").arg(m_params.captureOrientation);
    }
    if (m_tunnelForward) {
        args << QString("tunnel_forward=true");
    }
    if (!m_params.crop.isEmpty()) {
        args << QString("crop=%1").arg(m_params.crop);
    }
    if (!m_params.control) {
        args << QString("control=false");
    }
    if (m_params.stayAwake) {
        args << QString("stay_awake=true");
    }
    if (!m_params.codecOptions.isEmpty()) {
        args << QString("codec_options=%1").arg(m_params.codecOptions);
    }
    if (!m_params.codecName.isEmpty()) {
        args << QString("encoder_name=%1").arg(m_params.codecName);
    }
    args << "audio=false";
    if (-1 != m_params.scid) {
        args << QString("scid=%1").arg(m_params.scid, 8, 16, QChar('0'));
    }

#ifdef SERVER_DEBUGGER
    qInfo("Server debugger waiting for a client on device port " SERVER_DEBUGGER_PORT "...");
#endif

    m_serverProcess.execute(m_params.serial, args);
    return true;
}

bool TcpServerHandler::start(TcpServerHandler::ServerParams params)
{
    m_params = params;
    qInfo() << "TcpServerHandler: Starting USB/TCP mode for" << m_params.serial;
    m_serverStartStep = SSS_PUSH;
    return startServerByStep();
}

bool TcpServerHandler::connectTo()
{
    if (SSS_RUNNING != m_serverStartStep) {
        qWarning("server not run");
        return false;
    }

    if (!m_tunnelForward && !m_videoSocket) {
        startAcceptTimeoutTimer();
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

void TcpServerHandler::timerEvent(QTimerEvent *event)
{
    if (event && m_acceptTimeoutTimer == event->timerId()) {
        stopAcceptTimeoutTimer();
        emit serverStarted(false);
    } else if (event && m_connectTimeoutTimer == event->timerId()) {
        onConnectTimer();
    }
}

VideoSocket* TcpServerHandler::removeVideoSocket()
{
    VideoSocket* socket = m_videoSocket;
    m_videoSocket = Q_NULLPTR;
    return socket;
}

QTcpSocket *TcpServerHandler::getControlSocket()
{
    return m_controlSocket;
}

void TcpServerHandler::stop()
{
    if (m_tunnelForward) {
        stopConnectTimeoutTimer();
    } else {
        stopAcceptTimeoutTimer();
    }

    if (m_controlSocket) {
        m_controlSocket->close();
        m_controlSocket->deleteLater();
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
    m_serverSocketCtrl.close();
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
        case SSS_ENABLE_TUNNEL_REVERSE_CTRL:
            stepSuccess = enableTunnelReverseCtrl();
            break;
        case SSS_ENABLE_TUNNEL_FORWARD:
            stepSuccess = enableTunnelForward();
            break;
        case SSS_ENABLE_TUNNEL_FORWARD_CTRL:
            stepSuccess = enableTunnelForwardCtrl();
            break;
        case SSS_EXECUTE_SERVER:
            stepSuccess = execute();
            break;
        default:
            break;
        }
    }

    if (!stepSuccess) {
        emit serverStarted(false);
    }
    return stepSuccess;
}

bool TcpServerHandler::readInfo(VideoSocket *videoSocket, QString &deviceName, QSize &size)
{
    QElapsedTimer timer;
    timer.start();
    unsigned char buf[DEVICE_NAME_FIELD_LENGTH + 12];
    while (videoSocket->bytesAvailable() <= (DEVICE_NAME_FIELD_LENGTH + 12)) {
        videoSocket->waitForReadyRead(300);
        if (timer.elapsed() > 3000) {
            qInfo("readInfo timeout");
            return false;
        }
    }

    qint64 len = videoSocket->read((char *)buf, sizeof(buf));
    if (len < DEVICE_NAME_FIELD_LENGTH + 12) {
        qInfo("Could not retrieve device information");
        return false;
    }
    buf[DEVICE_NAME_FIELD_LENGTH - 1] = '\0';
    deviceName = QString::fromUtf8((const char *)buf);

    size.setWidth(bufferRead32be(&buf[DEVICE_NAME_FIELD_LENGTH + 4]));
    size.setHeight(bufferRead32be(&buf[DEVICE_NAME_FIELD_LENGTH + 8]));

    return true;
}

void TcpServerHandler::checkBothConnected()
{
    if (m_videoSocket && m_videoSocket->isValid() &&
        m_controlSocket && m_controlSocket->isValid()) {
        stopAcceptTimeoutTimer();
        disableTunnelReverse();
        m_tunnelEnabled = false;
        emit serverStarted(true, m_deviceName, m_deviceSize);
    }
}

void TcpServerHandler::startAcceptTimeoutTimer()
{
    stopAcceptTimeoutTimer();
    m_acceptTimeoutTimer = startTimer(1000);
}

void TcpServerHandler::stopAcceptTimeoutTimer()
{
    if (m_acceptTimeoutTimer) {
        killTimer(m_acceptTimeoutTimer);
        m_acceptTimeoutTimer = 0;
    }
}

void TcpServerHandler::startConnectTimeoutTimer()
{
    stopConnectTimeoutTimer();
    m_connectTimeoutTimer = startTimer(300);
}

void TcpServerHandler::stopConnectTimeoutTimer()
{
    if (m_connectTimeoutTimer) {
        killTimer(m_connectTimeoutTimer);
        m_connectTimeoutTimer = 0;
    }
    m_connectCount = 0;
}

void TcpServerHandler::onConnectTimer()
{
    QString deviceName;
    QSize deviceSize;
    bool success = false;

    VideoSocket *videoSocket = new VideoSocket();
    QTcpSocket *controlSocket = new QTcpSocket();

    videoSocket->connectToHost(QHostAddress::LocalHost, m_params.localPort);
    if (!videoSocket->waitForConnected(1000)) {
        m_connectCount = MAX_CONNECT_COUNT;
        qWarning("video socket connect to server failed");
        goto result;
    }
    videoSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // TCP_NODELAY

    controlSocket->connectToHost(QHostAddress::LocalHost, m_params.localPortCtrl);
    if (!controlSocket->waitForConnected(1000)) {
        m_connectCount = MAX_CONNECT_COUNT;
        qWarning("control socket connect to server failed");
        goto result;
    }
    controlSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // TCP_NODELAY

    if (QTcpSocket::ConnectedState == videoSocket->state()) {
        videoSocket->waitForReadyRead(1000);
        QByteArray data = videoSocket->read(1);
        if (!data.isEmpty() && readInfo(videoSocket, deviceName, deviceSize)) {
            success = true;
            goto result;
        } else {
            qWarning("video socket connect to server read device info failed, try again");
            goto result;
        }
    } else {
        qWarning("connect to server failed");
        m_connectCount = MAX_CONNECT_COUNT;
        goto result;
    }

result:
    if (success) {
        stopConnectTimeoutTimer();
        m_videoSocket = videoSocket;
        controlSocket->read(1);
        m_controlSocket = controlSocket;
        disableTunnelForward();
        m_tunnelEnabled = false;
        m_restartCount = 0;
        emit serverStarted(success, deviceName, deviceSize);
        return;
    }

    if (videoSocket) {
        videoSocket->deleteLater();
    }
    if (controlSocket) {
        controlSocket->deleteLater();
    }

    if (MAX_CONNECT_COUNT <= m_connectCount++) {
        stopConnectTimeoutTimer();
        stop();
        if (MAX_RESTART_COUNT > m_restartCount++) {
            qWarning("restart server auto");
            start(m_params);
        } else {
            m_restartCount = 0;
            emit serverStarted(false);
        }
    }
}

void TcpServerHandler::onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult)
{
    if (sender() == &m_workProcess) {
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
                    qCritical("adb push failed");
                    m_serverStartStep = SSS_NULL;
                    emit serverStarted(false);
                }
                break;
            case SSS_ENABLE_TUNNEL_REVERSE:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = SSS_ENABLE_TUNNEL_REVERSE_CTRL;
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    qCritical("adb reverse (video) failed, try forward");
                    m_tunnelForward = true;
                    m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                    startServerByStep();
                }
                break;
            case SSS_ENABLE_TUNNEL_REVERSE_CTRL:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverSocket.setMaxPendingConnections(1);
                    if (!m_serverSocket.listen(QHostAddress::LocalHost, m_params.localPort)) {
                        qCritical() << QString("Could not listen on video port %1").arg(m_params.localPort).toStdString().c_str();
                        m_serverStartStep = SSS_NULL;
                        disableTunnelReverse();
                        emit serverStarted(false);
                        break;
                    }
                    m_serverSocketCtrl.setMaxPendingConnections(1);
                    if (!m_serverSocketCtrl.listen(QHostAddress::LocalHost, m_params.localPortCtrl)) {
                        qCritical() << QString("Could not listen on control port %1").arg(m_params.localPortCtrl).toStdString().c_str();
                        m_serverSocket.close();
                        m_serverStartStep = SSS_NULL;
                        disableTunnelReverse();
                        emit serverStarted(false);
                        break;
                    }

                    m_serverStartStep = SSS_EXECUTE_SERVER;
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    qCritical("adb reverse (control) failed, try forward");
                    disableTunnelReverse();
                    m_tunnelForward = true;
                    m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                    startServerByStep();
                }
                break;
            case SSS_ENABLE_TUNNEL_FORWARD:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD_CTRL;
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    qCritical("adb forward (video) failed");
                    m_serverStartStep = SSS_NULL;
                    emit serverStarted(false);
                }
                break;
            case SSS_ENABLE_TUNNEL_FORWARD_CTRL:
                if (qsc::AdbProcess::AER_SUCCESS_EXEC == processResult) {
                    m_serverStartStep = SSS_EXECUTE_SERVER;
                    startServerByStep();
                } else if (qsc::AdbProcess::AER_SUCCESS_START != processResult) {
                    qCritical("adb forward (control) failed");
                    disableTunnelForward();
                    m_serverStartStep = SSS_NULL;
                    emit serverStarted(false);
                }
                break;
            default:
                break;
            }
        }
    }
    if (sender() == &m_serverProcess) {
        if (SSS_EXECUTE_SERVER == m_serverStartStep) {
            if (qsc::AdbProcess::AER_SUCCESS_START == processResult) {
                m_serverStartStep = SSS_RUNNING;
                m_tunnelEnabled = true;
                connectTo();
            } else if (qsc::AdbProcess::AER_ERROR_START == processResult) {
                if (!m_tunnelForward) {
                    m_serverSocket.close();
                    disableTunnelReverse();
                } else {
                    disableTunnelForward();
                }
                qCritical("adb shell start server failed");
                m_serverStartStep = SSS_NULL;
                emit serverStarted(false);
            }
        } else if (SSS_RUNNING == m_serverStartStep) {
            m_serverStartStep = SSS_NULL;
            emit serverStoped();
        }
    }
}
