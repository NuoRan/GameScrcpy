#pragma once
#include <QPointer>
#include <QMouseEvent>
#include <QImage>
#include <functional>

#include "GameScrcpyCoreDef.h"

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
 * 直接管理 DeviceSession，UI 通过 getSession() 获取 DeviceSession，然后用信号槽交互。
 * Directly manages DeviceSession; UI gets session via getSession() and interacts via signals/slots.
 */
class IDeviceManage : public QObject {
    Q_OBJECT
public:
    static IDeviceManage& getInstance();

    /**
     * @brief 连接设备
     * @param params 设备参数
     * @return 成功返回 true
     */
    virtual bool connectDevice(DeviceParams params) = 0;

    /**
     * @brief 断开设备
     * @param serial 设备序列号
     * @return 成功返回 true
     */
    virtual bool disconnectDevice(const QString &serial) = 0;

    /**
     * @brief 断开所有设备
     */
    virtual void disconnectAllDevice() = 0;

    /**
     * @brief 获取设备会话
     * @param serial 设备序列号
     * @return DeviceSession 指针，不存在返回 nullptr
     */
    virtual qsc::core::DeviceSession* getSession(const QString& serial) = 0;

signals:
    void deviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void deviceDisconnected(QString serial);
};

}
