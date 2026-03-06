#define LOG_TAG "DeviceMsg"
#include "Logger.h"

#include "devicemsg.h"

DeviceMsg::DeviceMsg() {}

DeviceMsg::~DeviceMsg() {}

DeviceMsg::DeviceMsgType DeviceMsg::type()
{
    return m_data.type;
}

int32_t DeviceMsg::deserialize(std::vector<uint8_t> &data)
{
    (void)data;
    // 暂无需要反序列化的设备消息
    return -1;
}
