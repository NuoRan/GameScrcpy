#include <QDir>
#include <QMessageBox>
#include <QTimer>
#include <QRegularExpression> // [新增] 正则解析需要

#include "controller.h"
#include "decoder.h"
#include "device.h"
#include "server.h"
#include "demuxer.h"
#include "kcpvideosocket.h"
#include "kcpcontrolsocket.h"
#include "videosocket.h"

namespace qsc {

    // ---------------------------------------------------------
    // 构造函数
    // 初始化解码器、控制器、流处理和服务器模块
    // ---------------------------------------------------------
    Device::Device(DeviceParams params, QObject *parent) : IDevice(parent), m_params(params)
    {
        // [新增] 初始化分辨率
        m_mobileSize = QSize(0, 0);

        // [新增] 初始化 ADB 进程对象用于获取分辨率
        m_adbSizeProcess = new qsc::AdbProcess(this);
        connect(m_adbSizeProcess, &qsc::AdbProcess::adbProcessResult, this, &Device::onAdbSizeResult);

        if (!params.display) {
            qCritical("must display");
            return;
        }

        if (params.display) {
            // 初始化解码器，设置帧回调
            m_decoder = new Decoder([this](int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV) {
                for (const auto& item : m_deviceObservers) {
                    item->onFrame(width, height, dataY, dataU, dataV, linesizeY, linesizeU, linesizeV);
                }
            }, this);

            // 初始化控制器，发送回调会在服务器启动后设置
            m_controller = new Controller([this](const QByteArray& buffer) -> qint64 {
                // 根据连接模式使用不同的 socket
                if (m_server) {
                    if (m_server->isWiFiMode()) {
                        // WiFi 模式：使用 KCP socket
                        if (m_server->getKcpControlSocket()) {
                            return m_server->getKcpControlSocket()->write(buffer);
                        }
                    } else {
                        // USB 模式：使用 TCP socket
                        if (m_server->getControlSocket()) {
                            return m_server->getControlSocket()->write(buffer);
                        }
                    }
                }
                return 0;
            }, params.gameScript, this);
        }

        m_stream = new Demuxer(this);
        m_server = new Server(this);

        initSignals();
    }

    Device::~Device()
    {
        Device::disconnectDevice();
    }

    // ---------------------------------------------------------
    // 用户数据与观察者管理
    // ---------------------------------------------------------
    void Device::setUserData(void *data)
    {
        m_userData = data;
    }

    void *Device::getUserData()
    {
        return m_userData;
    }

    void Device::registerDeviceObserver(DeviceObserver *observer)
    {
        m_deviceObservers.insert(observer);
    }

    void Device::deRegisterDeviceObserver(DeviceObserver *observer)
    {
        m_deviceObservers.erase(observer);
    }

    const QString &Device::getSerial()
    {
        return m_params.serial;
    }

    // ---------------------------------------------------------
    // 脚本与控制更新
    // ---------------------------------------------------------
    void Device::updateScript(QString script)
    {
        if (m_controller) {
            m_controller->updateScript(script);
        }
    }

    // ---------------------------------------------------------
    // 截图功能
    // 从解码器获取当前帧数据
    // ---------------------------------------------------------
    void Device::screenshot()
    {
        if (!m_decoder) {
            return;
        }
        m_decoder->peekFrame([this](int width, int height, uint8_t* dataRGB32) {
            saveFrame(width, height, dataRGB32);
        });
    }

    // 显示触摸点 (开发者选项)
    void Device::showTouch(bool show)
    {
        AdbProcess *adb = new qsc::AdbProcess();
        if (!adb) {
            return;
        }
        connect(adb, &qsc::AdbProcess::adbProcessResult, this, [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
            if (AdbProcess::AER_SUCCESS_START != processResult) {
                sender()->deleteLater();
            }
        });
        adb->setShowTouchesEnabled(getSerial(), show);
        qInfo() << getSerial() << " show touch " << (show ? "enable" : "disable");
    }

    bool Device::isReversePort(quint16 port)
    {
        Q_UNUSED(port);
        // 不再使用 adb reverse，总是返回 false
        return false;
    }

    // ---------------------------------------------------------
    // 信号初始化
    // 绑定各子模块（Controller, Server, Stream）的信号
    // ---------------------------------------------------------
    void Device::initSignals()
    {
        if (m_controller) {
            connect(m_controller, &Controller::grabCursor, this, [this](bool grab){
                for (const auto& item : m_deviceObservers) {
                    item->grabCursor(grab);
                }
            });
        }

        if (m_server) {
            // 服务器启动成功回调
            connect(m_server, &Server::serverStarted, this, [this](bool success, const QString &deviceName, const QSize &size) {
                m_serverStartSuccess = success;
                emit deviceConnected(success, m_params.serial, deviceName, size);
                if (success) {
                    // 初始化解码器和流读取
                    if (m_decoder) m_decoder->open();

                    // 根据连接模式安装相应的 socket
                    if (m_server->isWiFiMode()) {
                        // WiFi 模式：使用 KCP (低延迟)
                        qInfo() << "Using KCP mode (WiFi) for video streaming";
                        m_stream->installKcpVideoSocket(m_server->removeKcpVideoSocket());
                    } else {
                        // USB 模式：使用 TCP (通过 adb forward)
                        qInfo() << "Using TCP mode (USB) for video streaming";
                        m_stream->installVideoSocket(m_server->removeVideoSocket());
                    }

                    m_stream->setFrameSize(size);
                    m_stream->startDecode();

                    // 根据连接模式处理控制消息接收
                    if (m_server->isWiFiMode()) {
                        // WiFi 模式：KCP 控制通道
                        if (m_server->getKcpControlSocket()) {
                            connect(m_server->getKcpControlSocket(), &KcpControlSocket::readyRead, this, [this](){
                                if (!m_server || !m_server->getKcpControlSocket()) return;
                                m_server->getKcpControlSocket()->readAll(); // 清空接收缓冲区
                            });
                        }
                        // 启动控制消息发送器（使用 KCP socket 模式）
                        if (m_controller) {
                            m_controller->setControlSocket(m_server->getKcpControlSocket());
                            m_controller->startSender();
                        }
                    } else {
                        // USB 模式：TCP 控制通道
                        if (m_server->getControlSocket()) {
                            connect(m_server->getControlSocket(), &QTcpSocket::readyRead, this, [this](){
                                if (!m_server || !m_server->getControlSocket()) return;
                                m_server->getControlSocket()->readAll(); // 清空接收缓冲区
                            });
                        }
                        // 启动控制消息发送器（使用 TCP socket 模式）
                        if (m_controller) {
                            m_controller->setTcpControlSocket(m_server->getControlSocket());
                            m_controller->startSender();
                        }
                    }
                } else {
                    m_server->stop();
                }
            });
            connect(m_server, &Server::serverStoped, this, [this]() {
                disconnectDevice();
            });
        }

        if (m_stream) {
            connect(m_stream, &Demuxer::onStreamStop, this, [this]() {
                disconnectDevice();
            });
            // 接收数据包并推送给解码器
            // 使用 DirectConnection 因为 packet 指针在 emit 后会被释放
            // 解码在 Demuxer 线程中同步完成
            connect(m_stream, &Demuxer::getFrame, this, [this](AVPacket *packet) {
                if (m_decoder && !m_decoder->push(packet)) {
                    qCritical("Could not send packet to decoder");
                }
            }, Qt::DirectConnection);
        }

        if (m_decoder) {
            connect(m_decoder, &Decoder::updateFPS, this, [this](quint32 fps) {
                for (const auto& item : m_deviceObservers) {
                    item->updateFPS(fps);
                }
            });
        }
    }

    // ---------------------------------------------------------
    // 连接设备
    // ---------------------------------------------------------
    bool Device::connectDevice()
    {
        if (!m_server || m_serverStartSuccess) {
            return false;
        }

        QTimer::singleShot(0, this, [this]() {
            m_startTimeCount.start();
            Server::ServerParams params;
            // 填充服务器启动参数
            params.serverLocalPath = m_params.serverLocalPath;
            params.serverRemotePath = m_params.serverRemotePath;
            params.serial = m_params.serial;
            params.maxSize = m_params.maxSize;
            params.bitRate = m_params.bitRate;
            params.maxFps = m_params.maxFps;
            params.captureOrientationLock = m_params.captureOrientationLock;
            params.captureOrientation = m_params.captureOrientation;
            params.stayAwake = m_params.stayAwake;
            params.serverVersion = m_params.serverVersion;
            params.logLevel = m_params.logLevel;
            params.codecOptions = m_params.codecOptions;
            params.codecName = m_params.codecName;
            params.scid = m_params.scid;
            params.kcpPort = m_params.kcpPort;
            params.localPort = m_params.localPort;
            params.localPortCtrl = m_params.localPortCtrl;
            params.useReverse = m_params.useReverse;
            params.crop = "";
            params.control = true;

            m_server->start(params);
        });

        // [新增] 在连接设备时尝试获取一次真实分辨率
        updateMobileSize();

        return true;
    }

    // ---------------------------------------------------------
    // 断开设备连接
    // ---------------------------------------------------------
    void Device::disconnectDevice()
    {
        if (!m_server) {
            return;
        }

        // Stop demuxer first - this will close the socket internally
        // which wakes up any waiting recv and allows thread to exit
        if (m_stream) {
            m_stream->stopDecode();
        }

        // Stop server (closes any remaining sockets)
        m_server->stop();
        m_server = Q_NULLPTR;

        if (m_decoder) {
            m_decoder->close();
        }

        if (m_serverStartSuccess) {
            emit deviceDisconnected(m_params.serial);
        }
        m_serverStartSuccess = false;
    }

    // 转发控制指令到 Controller
    void Device::postGoBack() { if(m_controller) m_controller->postGoBack(); }
    void Device::postGoHome() { if(m_controller) m_controller->postGoHome(); }
    void Device::postGoMenu() { if(m_controller) m_controller->postGoMenu(); }
    void Device::postAppSwitch() { if(m_controller) m_controller->postAppSwitch(); }
    void Device::postPower() { if(m_controller) m_controller->postPower(); }
    void Device::postVolumeUp() { if(m_controller) m_controller->postVolumeUp(); }
    void Device::postVolumeDown() { if(m_controller) m_controller->postVolumeDown(); }
    void Device::postBackOrScreenOn(bool down) { if(m_controller) m_controller->postBackOrScreenOn(down); }

    void Device::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
    {
        if (m_controller) m_controller->mouseEvent(from, frameSize, showSize);
    }

    void Device::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
    {
        if (m_controller) m_controller->wheelEvent(from, frameSize, showSize);
    }

    void Device::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
    {
        if (m_controller) m_controller->keyEvent(from, frameSize, showSize);
    }

    bool Device::isCurrentCustomKeymap()
    {
        if (!m_controller) return false;
        return m_controller->isCurrentCustomKeymap();
    }

    // 保存当前帧为图片
    bool Device::saveFrame(int width, int height, uint8_t* dataRGB32)
    {
        if (!dataRGB32) return false;

        QImage rgbImage(dataRGB32, width, height, QImage::Format_RGB32);

        QString fileDir(m_params.recordPath);
        if (fileDir.isEmpty()) {
            qWarning() << "please select record save path!!!";
            return false;
        }
        QDateTime dateTime = QDateTime::currentDateTime();
        QString fileName = dateTime.toString("_yyyyMMdd_hhmmss_zzz");
        fileName = m_params.serial + fileName;
        fileName.replace(":", "_");
        fileName.replace(".", "_");
        fileName += ".png";
        QDir dir(fileDir);
        QString absFilePath = dir.absoluteFilePath(fileName);
        int ret = rgbImage.save(absFilePath, "PNG", 100);
        if (!ret) {
            return false;
        }

        qInfo() << "screenshot save to " << absFilePath;
        return true;
    }

    // [新增] 实现获取手机实际分辨率
    QSize Device::getMobileSize()
    {
        return m_mobileSize;
    }

    // [新增] 触发更新分辨率命令
    void Device::updateMobileSize()
    {
        if (!m_adbSizeProcess) return;
        // 执行 adb shell wm size
        QStringList args;
        args << "shell" << "wm" << "size";
        m_adbSizeProcess->execute(m_params.serial, args);
    }

    // [新增] 解析 ADB 返回结果
    void Device::onAdbSizeResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult)
    {
        if (qsc::AdbProcess::AER_SUCCESS_EXEC != processResult) {
            return;
        }

        QString output = m_adbSizeProcess->getStdOut();
        if (output.isEmpty()) return;

        // 优先匹配 Override size (用户修改过的分辨率)
        QRegularExpression regexOverride("Override size:\\s*(\\d+)x(\\d+)");
        // 其次匹配 Physical size (物理分辨率)
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

                // [新增] 将获取到的分辨率传递给 Controller -> InputConvert
                if (m_controller) {

                    m_controller->setMobileSize(m_mobileSize);
                }
            }
        } else {
            qWarning() << "Failed to parse wm size output:" << output;
        }
    }

    void Device::pushFileRequest(const QString &file, const QString &devicePath)
    {
        Q_UNUSED(file);
        Q_UNUSED(devicePath);
        // TODO: Implement file push functionality
    }

    void Device::installApkRequest(const QString &apkFile)
    {
        Q_UNUSED(apkFile);
        // TODO: Implement APK install functionality
    }

    // [新增] 设置帧获取回调 (用于脚本图像识别)
    void Device::setFrameGrabCallback(std::function<QImage()> callback)
    {
        // 存储回调，以便在 InputConvertGame 重建时可以重新设置
        m_frameGrabCallback = callback;

        // 立即转发到 Controller
        if (m_controller) {
            m_controller->setFrameGrabCallback(callback);
        }
    }
}
