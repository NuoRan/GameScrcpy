#include "devicemsg.h"
#include "receiver.h"

Receiver::Receiver(QObject *parent) : QObject(parent) {}

Receiver::~Receiver() {}

void Receiver::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    Q_UNUSED(deviceMsg);
    // 暂无需要处理的设备消息
}
