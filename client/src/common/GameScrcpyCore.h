#pragma once
#include <functional>
#include <vector>
#include <string>

#include "GameScrcpyCoreDef.h"
#include "GameTypes.h"

// 前向声明
namespace qsc {
namespace core {
class DeviceSession;
}
}

namespace qsc {

/**
 * @brief 设备管理接口 / Device Management Interface
 *
 * 直接管理 DeviceSession，UI 通过 getSession() 获取 DeviceSession，然后用回调交互。
 * Directly manages DeviceSession; UI gets session via getSession() and interacts via callbacks.
 */
class IDeviceManage {
public:
    static IDeviceManage& getInstance();
    virtual ~IDeviceManage() = default;

    // === 回调类型 / Callback types ===
    using DeviceConnectedCb = std::function<void(bool success, const std::string& serial,
                                                  const std::string& deviceName, const Size& size)>;
    using DeviceDisconnectedCb = std::function<void(const std::string& serial)>;

    /// 注册设备连接监听器，返回 listener ID / Register listener, returns ID
    virtual int addDeviceConnectedListener(DeviceConnectedCb cb) = 0;
    /// 注册设备断开监听器，返回 listener ID / Register listener, returns ID
    virtual int addDeviceDisconnectedListener(DeviceDisconnectedCb cb) = 0;
    /// 移除监听器 / Remove listener by ID
    virtual void removeDeviceListener(int id) = 0;

    virtual bool connectDevice(DeviceParams params) = 0;
    virtual bool disconnectDevice(const std::string &serial) = 0;
    virtual void disconnectAllDevice() = 0;
    virtual qsc::core::DeviceSession* getSession(const std::string& serial) = 0;
    virtual void updateDeviceResolution(const std::string& serial, const Size& size) = 0;

protected:
    IDeviceManage() = default;
};

}
