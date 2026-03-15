#ifndef DEVICEMANAGE_H
#define DEVICEMANAGE_H

#include <map>

#include <memory>
#include <atomic>

#include "GameScrcpyCore.h"
#include "GameSignal.h"
#include "adbprocess.h"

#include "GameTypes.h"

// 前向声明
class Server;
class AuxChannelClient;
class AudioStreamManager;
class TouchRouter;
class ITouchBackend;

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
class DeviceController
{
public:
    explicit DeviceController(const DeviceParams& params);
    ~DeviceController();

    bool start();
    void stop();

    const std::string& serial() const { return m_params.serial; }
    core::DeviceSession* session() const { return m_session.get(); }
    bool isReversePort(uint16_t port) const;

    Signal<bool, const std::string&, const std::string&, const Size&> connected;
    Signal<const std::string&> disconnected;

private:
    void onServerStart(bool success, const std::string& deviceName, const Size& size);
    void onServerStop();
    void onAdbSizeResult(AdbProcess::ADB_EXEC_RESULT processResult);
    bool startPureAoa();
    bool startPureEsp32();

private:
    DeviceParams m_params;
    std::unique_ptr<core::DeviceSession> m_session;
    std::unique_ptr<core::ZeroCopyStreamManager> m_streamManager;
    Server* m_server = nullptr;
    AuxChannelClient* m_auxChannel = nullptr;
    AudioStreamManager* m_audioManager = nullptr;
    AdbProcess* m_adbSizeProcess = nullptr;
    TouchRouter* m_touchRouter = nullptr;
    ITouchBackend* m_hidBackend = nullptr;
    Size m_mobileSize;
    bool m_stopping = false;
    std::shared_ptr<std::atomic<bool>> m_aliveToken;
};

/**
 * @brief 设备管理器 / Device Manager
 *
 * 管理所有设备的连接，提供全局访问接口。
 * Manages all device connections and provides global access interface.
 */
class DeviceManage : public IDeviceManage
{
public:
    explicit DeviceManage();
    ~DeviceManage() override;

    // IDeviceManage 接口
    bool connectDevice(DeviceParams params) override;
    bool disconnectDevice(const std::string &serial) override;
    void disconnectAllDevice() override;
    core::DeviceSession* getSession(const std::string& serial) override;

    // 回调注册
    int addDeviceConnectedListener(DeviceConnectedCb cb) override;
    int addDeviceDisconnectedListener(DeviceDisconnectedCb cb) override;
    void removeDeviceListener(int id) override;

private:
    void onDeviceConnected(bool success, const std::string& serial, const std::string& deviceName, const Size& size);
    void onDeviceDisconnected(const std::string& serial);
    void notifyDeviceConnected(bool success, const std::string& serial, const std::string& deviceName, const Size& size);
    void notifyDeviceDisconnected(const std::string& serial);

private:
    uint16_t getFreePort();
    void removeDevice(const std::string& serial);

private:
    std::map<std::string, DeviceController*> m_devices;
    uint16_t m_localPortStart = 27183;

    struct ListenerEntry { int id; int type; };  // type: 0=connected, 1=disconnected
    std::vector<std::pair<int, DeviceConnectedCb>> m_connectedListeners;
    std::vector<std::pair<int, DeviceDisconnectedCb>> m_disconnectedListeners;
    int m_nextListenerId = 1;
};

}
#endif // DEVICEMANAGE_H
