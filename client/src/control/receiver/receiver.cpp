#include "devicemsg.h"
#include "receiver.h"

Receiver::Receiver() {}

Receiver::~Receiver() {}

void Receiver::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    (void)deviceMsg;
    // 暂无需要处理的设备消息
}
