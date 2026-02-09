#include <QDebug>

#include "devicemsg.h"

DeviceMsg::DeviceMsg(QObject *parent) : QObject(parent) {}

DeviceMsg::~DeviceMsg() {}

DeviceMsg::DeviceMsgType DeviceMsg::type()
{
    return m_data.type;
}

qint32 DeviceMsg::deserialize(QByteArray &byteArray)
{
    Q_UNUSED(byteArray);
    // 暂无需要反序列化的设备消息
    return -1;
}
