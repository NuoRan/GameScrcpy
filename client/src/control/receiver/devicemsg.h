#ifndef DEVICEMSG_H
#define DEVICEMSG_H

#include <vector>
#include <cstdint>

#define DEVICE_MSG_MAX_SIZE (1 << 18) // 256k

/**
 * @brief 设备消息解析器 / Device Message Parser
 *
 * 解析从 Android 设备接收到的控制响应消息。
 * Parses control response messages received from Android device.
 */
class DeviceMsg
{
public:
    enum DeviceMsgType
    {
        DMT_NULL = -1,
    };
    DeviceMsg();
    virtual ~DeviceMsg();

    DeviceMsg::DeviceMsgType type();
    int32_t deserialize(std::vector<uint8_t> &data);

private:
    struct DeviceMsgData
    {
        DeviceMsgType type = DMT_NULL;
    };

    DeviceMsgData m_data;
};

#endif // DEVICEMSG_H
