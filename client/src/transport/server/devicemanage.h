#ifndef DEVICEMANAGE_H
#define DEVICEMANAGE_H

#include <QMap>
#include <QPointer>
#include <memory>

#include "GameScrcpyCore.h"
#include "adbprocess.h"

// 前向声明
class Server;

namespace qsc {
namespace core {
class DeviceSession;
class ZeroCopyStreamManager;
}

/**
 * @brief 设备控制器 / Device Controller
 *
 * 管理单个设备的连接生命周期 / Manages single device connection lifecycle:
 * - Server（启动/停止）/ Server (start/stop)
 * - DeviceSession（会话）/ DeviceSession (session)
 * - ZeroCopyStreamManager（视频流）/ ZeroCopyStreamManager (video stream)
 */
class DeviceController : public QObject
{
    Q_OBJECT

public:
    explicit DeviceController(const DeviceParams& params, QObject* parent = nullptr);
    ~DeviceController();

    bool start();
    void stop();

    const QString& serial() const { return m_params.serial; }
    core::DeviceSession* session() const { return m_session.get(); }
    bool isReversePort(quint16 port) const;

signals:
    void connected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void disconnected(const QString& serial);

private slots:
    void onServerStart(bool success, const QString& deviceName, const QSize& size);
    void onServerStop();
    void onAdbSizeResult(AdbProcess::ADB_EXEC_RESULT processResult);

private:
    DeviceParams m_params;
    std::unique_ptr<core::DeviceSession> m_session;
    std::unique_ptr<core::ZeroCopyStreamManager> m_streamManager;
    QPointer<Server> m_server;
    AdbProcess* m_adbSizeProcess = nullptr;
    QSize m_mobileSize;
};

/**
 * @brief 设备管理器 / Device Manager
 *
 * 管理所有设备的连接，提供全局访问接口。
 * Manages all device connections and provides global access interface.
 */
class DeviceManage : public IDeviceManage
{
    Q_OBJECT

public:
    explicit DeviceManage();
    ~DeviceManage() override;

    // IDeviceManage 接口
    bool connectDevice(DeviceParams params) override;
    bool disconnectDevice(const QString &serial) override;
    void disconnectAllDevice() override;
    core::DeviceSession* getSession(const QString& serial) override;

private slots:
    void onDeviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void onDeviceDisconnected(const QString& serial);

private:
    quint16 getFreePort();
    void removeDevice(const QString& serial);

private:
    QMap<QString, DeviceController*> m_devices;
    quint16 m_localPortStart = 27183;
};

}
#endif // DEVICEMANAGE_H
