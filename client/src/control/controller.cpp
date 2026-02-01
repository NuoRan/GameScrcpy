#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QDebug>

#include "controller.h"
#include "controlmsg.h"
#include "controlmsgpool.h"
#include "controlsender.h"
#include "inputconvertgame.h"
#include "kcpcontrolsocket.h"

// ---------------------------------------------------------
// 构造函数
// 初始化输入转换模块和异步发送器
// ---------------------------------------------------------
Controller::Controller(KcpSendCallback sendCallback, QString gameScript, QObject *parent)
    : QObject(parent)
    , m_sendCallback(sendCallback)
{
    // 创建异步发送器
    m_controlSender = new ControlSender(this);
    m_controlSender->setSendCallback(sendCallback);

    // 连接异步发送器的信号
    connect(m_controlSender, &ControlSender::sendError, this, [](const QString &error) {
        qWarning() << "[Controller] Send error:" << error;
    });
    connect(m_controlSender, &ControlSender::bufferWarning, this, [](int pending, int threshold) {
        qWarning() << "[Controller] Buffer warning:" << pending << "/" << threshold << "bytes";
    });

    updateScript(gameScript);
}

Controller::~Controller()
{
    stopSender();
}

void Controller::startSender()
{
    if (m_controlSender) {
        m_controlSender->start();
    }
}

void Controller::stopSender()
{
    // 先发送断开消息通知服务端
    postDisconnect();

    if (m_controlSender) {
        m_controlSender->stop();
    }
}

void Controller::postControlMsg(ControlMsg *controlMsg)
{
    if (controlMsg) {
        QByteArray data = controlMsg->serializeData();
        if (m_controlSender) {
            m_controlSender->send(data);
        }
        // 【优化】使用对象池回收 ControlMsg，减少内存分配开销
        ControlMsgPool::instance().release(controlMsg);
    }
}

// ---------------------------------------------------------
// FastMsg 协议快速发送
// 直接发送已序列化的二进制数据，无需构造 ControlMsg 对象
// ---------------------------------------------------------
void Controller::postFastMsg(const QByteArray &data)
{
    if (data.isEmpty()) return;

    if (m_controlSender) {
        m_controlSender->send(data);
    }
}

void Controller::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    Q_UNUSED(deviceMsg);
}

void Controller::test(QRect rc)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(POINTER_ID_MOUSE), AMOTION_EVENT_ACTION_DOWN, AMOTION_EVENT_BUTTON_PRIMARY, AMOTION_EVENT_BUTTON_PRIMARY, rc, 1.0f);
    postControlMsg(controlMsg);
}

// ---------------------------------------------------------
// 更新键位映射脚本
// [修改] 强制始终使用 InputConvertGame，不再创建 InputConvertNormal
// ---------------------------------------------------------
void Controller::updateScript(QString gameScript)
{
    if (m_inputConvert) {
        delete m_inputConvert;
        m_inputConvert = nullptr;
    }

    // 无论有没有脚本，都使用 InputConvertGame
    // 如果没有脚本，InputConvertGame 内部 m_keyMap 为空，但会处理光标逻辑
    InputConvertGame *convertgame = new InputConvertGame(this);

    if (!gameScript.isEmpty()) {
        convertgame->loadKeyMap(gameScript);
    }

    m_inputConvert = convertgame;

    // [修复] 3. 新对象创建好了，赶紧把刚才记住的分辨率塞进去！
    if (m_inputConvert && m_mobileSize.isValid()) {
        m_inputConvert->setMobileSize(m_mobileSize);
    }

    // [修复] 重新设置帧获取回调 (用于脚本图像识别)
    if (m_frameGrabCallback) {
        convertgame->setFrameGrabCallback(m_frameGrabCallback);
    }

    Q_ASSERT(m_inputConvert);
    connect(m_inputConvert, &InputConvertBase::grabCursor, this, &Controller::grabCursor);
}

bool Controller::isCurrentCustomKeymap()
{
    if (!m_inputConvert) {
        return false;
    }
    return m_inputConvert->isCurrentCustomKeymap();
}

// ---------------------------------------------------------
// Android 功能快捷键实现
// ---------------------------------------------------------
void Controller::postBackOrScreenOn(bool down)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_BACK_OR_SCREEN_ON);
    controlMsg->setBackOrScreenOnData(down);
    if (!controlMsg) return;
    postControlMsg(controlMsg);
}

void Controller::postGoHome() { postKeyCodeClick(AKEYCODE_HOME); }
void Controller::postGoMenu() { postKeyCodeClick(AKEYCODE_MENU); }
void Controller::postGoBack() { postKeyCodeClick(AKEYCODE_BACK); }
void Controller::postAppSwitch() { postKeyCodeClick(AKEYCODE_APP_SWITCH); }
void Controller::postPower() { postKeyCodeClick(AKEYCODE_POWER); }
void Controller::postVolumeUp() { postKeyCodeClick(AKEYCODE_VOLUME_UP); }
void Controller::postVolumeDown() { postKeyCodeClick(AKEYCODE_VOLUME_DOWN); }

// ---------------------------------------------------------
// 输入事件转发
// 将 Qt 事件传递给 InputConvert 进行处理
// ---------------------------------------------------------
void Controller::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputConvert) m_inputConvert->mouseEvent(from, frameSize, showSize);
}

void Controller::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputConvert) m_inputConvert->wheelEvent(from, frameSize, showSize);
}

void Controller::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputConvert) m_inputConvert->keyEvent(from, frameSize, showSize);
}

// ---------------------------------------------------------
// 事件处理循环
// 处理 ControlMsg 事件，序列化并发送数据
// ---------------------------------------------------------
bool Controller::event(QEvent *event)
{
    if (event && static_cast<ControlMsg::Type>(event->type()) == ControlMsg::Control) {
        ControlMsg *controlMsg = dynamic_cast<ControlMsg *>(event);
        if (controlMsg) {
            sendControl(controlMsg->serializeData());
        }
        return true;
    }
    return QObject::event(event);
}

// ---------------------------------------------------------
// 发送控制数据（非阻塞）
// 使用智能缓冲区管理，避免阻塞主线程
// ---------------------------------------------------------
bool Controller::sendControl(const QByteArray &buffer)
{
    if (buffer.isEmpty()) return false;

    if (!m_controlSender) {
        qWarning() << "[Controller] No sender available";
        return false;
    }

    // 非阻塞发送，缓冲区满时会丢弃消息
    return m_controlSender->send(buffer);
}

// 发送完整的按键点击动作 (按下 + 抬起)
void Controller::postKeyCodeClick(AndroidKeycode keycode)
{
    ControlMsg *controlEventDown = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (controlEventDown) {
        controlEventDown->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, keycode, 0, AMETA_NONE);
        postControlMsg(controlEventDown);
    }

    ControlMsg *controlEventUp = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (controlEventUp) {
        controlEventUp->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_UP, keycode, 0, AMETA_NONE);
        postControlMsg(controlEventUp);
    }
}

// [新增] 实现
void Controller::setMobileSize(const QSize &size)
{
    m_mobileSize = size; // 【必须有这一行】保存到成员变量
    if (m_inputConvert) {
        m_inputConvert->setMobileSize(size);
    }
}

// [新增] 设置控制 socket (WiFi 模式)，启用非阻塞发送模式
void Controller::setControlSocket(KcpControlSocket *socket)
{
    if (m_controlSender && socket) {
        // 切换到 KCP socket 模式
        m_controlSender->setSocket(socket);
        // 清除回调，使用 socket 直接发送
        m_controlSender->setSendCallback(nullptr);
    }
}

// [新增] 设置 TCP 控制 socket (USB 模式)
void Controller::setTcpControlSocket(QTcpSocket *socket)
{
    if (m_controlSender && socket) {
        // 切换到 TCP socket 模式
        m_controlSender->setTcpSocket(socket);
        // 清除回调，使用 socket 直接发送
        m_controlSender->setSendCallback(nullptr);
    }
}

// [新增] 设置帧获取回调 (用于脚本图像识别)
void Controller::setFrameGrabCallback(std::function<QImage()> callback)
{
    // 存储回调，以便在 InputConvertGame 重建时可以重新设置
    m_frameGrabCallback = callback;

    // 立即转发到当前的 InputConvertGame
    InputConvertGame *gameConvert = qobject_cast<InputConvertGame*>(m_inputConvert.data());
    if (gameConvert) {
        gameConvert->setFrameGrabCallback(callback);
    }
}

// [新增] 发送断开消息给服务端
void Controller::postDisconnect()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_DISCONNECT);
    if (controlMsg) {
        // 直接发送，不用异步模式，因为这是关闭前的最后一个消息
        if (m_controlSender) {
            m_controlSender->send(controlMsg->serializeData());
        }
        delete controlMsg;
        qInfo() << "[Controller] Sent disconnect message to server";
    }
}
