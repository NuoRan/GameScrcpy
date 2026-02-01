#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QImage>
#include <QTcpSocket>
#include <functional>

#include "inputconvertbase.h"

class KcpControlSocket;
class Receiver;
class InputConvertBase;
class DeviceMsg;
class ControlSender;

// KCP 发送回调函数类型
using KcpSendCallback = std::function<qint64(const QByteArray&)>;

// ---------------------------------------------------------
// 控制器类
// 协调输入转换模块和网络发送模块，处理具体的设备控制逻辑
// ---------------------------------------------------------
class Controller : public QObject
{
    Q_OBJECT
public:
    Controller(KcpSendCallback sendCallback, QString gameScript = "", QObject *parent = Q_NULLPTR);
    virtual ~Controller();

    void startSender();
    void stopSender();

    void postControlMsg(ControlMsg *controlMsg);
    void postFastMsg(const QByteArray &data);  // FastMsg 协议快速发送
    void recvDeviceMsg(DeviceMsg *deviceMsg);
    void test(QRect rc);

    // 脚本管理
    void updateScript(QString gameScript = "");
    bool isCurrentCustomKeymap();

    // Android 常用功能快捷接口
    void postGoBack();
    void postGoHome();
    void postGoMenu();
    void postAppSwitch();
    void postPower();
    void postVolumeUp();
    void postVolumeDown();

    // 输入转换接口
    void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);

    // 返回键/亮屏
    void postBackOrScreenOn(bool down);

    // 【核心】发送按键点击事件 (down + up)
    void postKeyCodeClick(AndroidKeycode keycode);

    // [新增] 设置移动设备分辨率
    void setMobileSize(const QSize &size);

    // [新增] 设置控制 socket（用于非阻塞发送）
    void setControlSocket(KcpControlSocket *socket);  // WiFi 模式 (KCP)
    void setTcpControlSocket(QTcpSocket *socket);     // USB 模式 (TCP)

    // [新增] 设置帧获取回调 (用于脚本图像识别)
    void setFrameGrabCallback(std::function<QImage()> callback);

    // [新增] 发送断开消息给服务端
    void postDisconnect();

signals:
    void grabCursor(bool grab);

protected:
    bool event(QEvent *event);

private:
    bool sendControl(const QByteArray &buffer);

private:
    KcpSendCallback m_sendCallback;
    QPointer<ControlSender> m_controlSender;
    QPointer<Receiver> m_receiver;
    QPointer<InputConvertBase> m_inputConvert;

    QSize m_mobileSize;
    // [新增] 存储帧获取回调 (InputConvertGame重建后需要重新设置)
    std::function<QImage()> m_frameGrabCallback;
};

#endif // CONTROLLER_H
