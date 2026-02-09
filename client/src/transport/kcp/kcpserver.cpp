#include <QCoreApplication>
#include <QDebug>
#include <QTimerEvent>
#include <QNetworkInterface>
#include <QHostAddress>

#include "kcpserver.h"

#define MAX_WAIT_COUNT 100  // 最多等待 100 * 100ms = 10秒
#define MAX_RESTART_COUNT 1

KcpServer::KcpServer(QObject *parent) : QObject(parent)
{
    connect(&m_workProcess, &qsc::AdbProcess::adbProcessResult, this, &KcpServer::onWorkProcessResult);
    connect(&m_serverProcess, &qsc::AdbProcess::adbProcessResult, this, &KcpServer::onWorkProcessResult);
}

KcpServer::~KcpServer() {}

bool KcpServer::killOldServer()
{
    if (m_workProcess.isRuning()) {
        m_workProcess.kill();
    }
    // 杀死设备上旧的 scrcpy 进程，避免端口占用
    QStringList args;
    args << "shell" << "pkill" << "-f" << "scrcpy";
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

    // capture_orientation
    if (1 == m_params.captureOrientationLock) {
        args << QString("capture_orientation=@%1").arg(m_params.captureOrientation);
    } else if (2 == m_params.captureOrientationLock) {
        args << QString("capture_orientation=@");
    } else {
        args << QString("capture_orientation=%1").arg(m_params.captureOrientation);
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

    // KCP 模式参数
    args << QString("use_kcp=true");
    args << QString("kcp_port=%1").arg(m_params.kcpPort);
    args << QString("kcp_control_port=%1").arg(m_params.kcpPort + 1);

    QString deviceIp = m_params.serial.split(':').first();
    QString clientIp = findClientIpInSameSubnet(deviceIp);
    args << QString("client_ip=%1").arg(clientIp);

#ifdef SERVER_DEBUGGER
    qInfo("Server debugger waiting for a client on device port " SERVER_DEBUGGER_PORT "...");
#endif

    m_serverProcess.execute(m_params.serial, args);
    return true;
}

bool KcpServer::start(KcpServer::ServerParams params)
{
    m_params = params;
    qInfo() << "KcpServer: Starting WiFi/KCP mode for" << m_params.serial;
    m_serverStartStep = SSS_KILL_SERVER;  // 先杀死旧进程
    return startServerByStep();
}

KcpServer::ServerParams KcpServer::getParams()
{
    return m_params;
}

void KcpServer::timerEvent(QTimerEvent *event)
{
    if (event && m_waitTimer == event->timerId()) {
        onWaitKcpTimer();
    }
}

KcpVideoSocket* KcpServer::removeKcpVideoSocket()
{
    KcpVideoSocket* socket = m_kcpVideoSocket;
    m_kcpVideoSocket = Q_NULLPTR;
    return socket;
}

KcpControlSocket* KcpServer::getKcpControlSocket()
{
    return m_kcpControlSocket;
}

void KcpServer::stop()
{
    stopWaitTimer();

    if (m_kcpControlSocket) {
        m_kcpControlSocket->close();
        m_kcpControlSocket->deleteLater();
        m_kcpControlSocket = Q_NULLPTR;
    }
    if (m_kcpVideoSocket) {
        m_kcpVideoSocket->close();
        m_kcpVideoSocket->deleteLater();
        m_kcpVideoSocket = Q_NULLPTR;
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
        emit serverStarted(false);
    }
    return stepSuccess;
}

QString KcpServer::findClientIpInSameSubnet(const QString &deviceIp) const
{
    QString clientIp;

    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsUp &&
            iface.flags() & QNetworkInterface::IsRunning &&
            !(iface.flags() & QNetworkInterface::IsLoopBack)) {
            for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    QStringList pcParts = entry.ip().toString().split('.');
                    QStringList devParts = deviceIp.split('.');
                    if (pcParts.size() == 4 && devParts.size() == 4 &&
                        pcParts[0] == devParts[0] && pcParts[1] == devParts[1] && pcParts[2] == devParts[2]) {
                        clientIp = entry.ip().toString();
                        return clientIp;
                    }
                }
            }
        }
    }

    for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            clientIp = addr.toString();
            break;
        }
    }

    return clientIp;
}

void KcpServer::startWaitTimer()
{
    if (m_waitTimer) {
        killTimer(m_waitTimer);
    }
    m_waitCount = 0;
    m_waitTimer = startTimer(100);
}

void KcpServer::stopWaitTimer()
{
    if (m_waitTimer) {
        killTimer(m_waitTimer);
        m_waitTimer = 0;
    }
    m_waitCount = 0;
}

void KcpServer::setupKcpSockets()
{
    QString serverIp = m_params.serial.split(':').first();

    // 创建 KCP video socket
    m_kcpVideoSocket = new KcpVideoSocket(nullptr);
    m_kcpVideoSocket->setBitrate(m_params.bitRate);
    if (!m_kcpVideoSocket->bind(m_params.kcpPort)) {
        qCritical() << "Failed to bind KCP video socket to port" << m_params.kcpPort;
        delete m_kcpVideoSocket;
        m_kcpVideoSocket = nullptr;
        emit serverStarted(false);
        return;
    }

    // 创建 KCP control socket
    m_kcpControlSocket = new KcpControlSocket(this);
    if (!m_kcpControlSocket->bind(m_params.kcpPort + 1)) {
        qCritical() << "Failed to bind KCP control socket to port" << (m_params.kcpPort + 1);
        emit serverStarted(false);
        return;
    }

    m_kcpControlSocket->connectToHost(QHostAddress(serverIp), m_params.kcpPort + 1);

    startWaitTimer();
}

void KcpServer::onWaitKcpTimer()
{
    if (m_kcpVideoSocket && m_kcpVideoSocket->isValid()) {
        qint64 avail = m_kcpVideoSocket->bytesAvailable();
        if (avail > 0 || m_waitCount >= 10) {
            stopWaitTimer();
            m_restartCount = 0;
            emit serverStarted(true, m_deviceName, m_deviceSize);
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
            emit serverStarted(false);
        }
    }
}

void KcpServer::onWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult)
{
    if (sender() == &m_workProcess) {
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
                    qCritical("adb push failed");
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
                setupKcpSockets();
            } else if (qsc::AdbProcess::AER_ERROR_START == processResult) {
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
