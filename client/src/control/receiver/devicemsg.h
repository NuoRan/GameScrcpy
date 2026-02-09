#ifndef DEVICEMSG_H
#define DEVICEMSG_H

#include <QBuffer>

#define DEVICE_MSG_MAX_SIZE (1 << 18) // 256k

/**
 * @brief 设备消息解析器 / Device Message Parser
 *
 * 解析从 Android 设备接收到的控制响应消息。
 * Parses control response messages received from Android device.
 */
class DeviceMsg : public QObject
{
    Q_OBJECT
public:
    enum DeviceMsgType
    {
        DMT_NULL = -1,
    };
    explicit DeviceMsg(QObject *parent = nullptr);
    virtual ~DeviceMsg();

    DeviceMsg::DeviceMsgType type();
    qint32 deserialize(QByteArray &byteArray);

private:
    struct DeviceMsgData
    {
        DeviceMsgType type = DMT_NULL;
    };

    DeviceMsgData m_data;
};

#endif // DEVICEMSG_H
