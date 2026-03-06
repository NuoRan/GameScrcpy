#ifndef RECEIVER_H
#define RECEIVER_H

class DeviceMsg;
/**
 * @brief 设备消息接收器 / Device Message Receiver
 *
 * 接收并处理来自 Android 设备的控制通道消息。
 * Receives and processes control channel messages from Android device.
 */
class Receiver
{
public:
    Receiver();
    virtual ~Receiver();

    void recvDeviceMsg(DeviceMsg *deviceMsg);
};

#endif // RECEIVER_H
