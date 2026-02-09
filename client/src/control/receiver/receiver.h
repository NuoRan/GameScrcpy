#ifndef RECEIVER_H
#define RECEIVER_H

#include <QPointer>

class DeviceMsg;
/**
 * @brief 设备消息接收器 / Device Message Receiver
 *
 * 接收并处理来自 Android 设备的控制通道消息。
 * Receives and processes control channel messages from Android device.
 */
class Receiver : public QObject
{
    Q_OBJECT
public:
    explicit Receiver(QObject *parent = Q_NULLPTR);
    virtual ~Receiver();

    void recvDeviceMsg(DeviceMsg *deviceMsg);
};

#endif // RECEIVER_H
