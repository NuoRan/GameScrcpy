#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include "controller.h"
#include "controlsender.h"
#include "SessionContext.h"
#include "kcpcontrolsocket.h"
#include "fastmsg.h"
#include "interfaces/IControlChannel.h"

// ---------------------------------------------------------
// 构造函数
// 初始化 SessionContext 和异步发送器
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

    updateScript(gameScript);
}

Controller::~Controller()
{
    stopSender();

    // 删除 SessionContext
    if (m_sessionContext) {
        delete m_sessionContext;
        m_sessionContext = nullptr;
    }
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

// ---------------------------------------------------------
// FastMsg 协议快速发送
// ---------------------------------------------------------
void Controller::postFastMsg(const QByteArray &data)
{
    if (data.isEmpty()) return;
    if (m_controlSender) {
        m_controlSender->send(data);
    }
}

void Controller::postFastMsg(const char *data, int len)
{
    if (!data || len <= 0) return;
    if (m_controlSender) {
        // 零分配快速路径 — 直接从栈缓冲区构造 QByteArray（浅引用）
        m_controlSender->send(QByteArray::fromRawData(data, len));
    }
}

void Controller::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    Q_UNUSED(deviceMsg);
}

// ---------------------------------------------------------
// 更新键位映射脚本
// ---------------------------------------------------------
void Controller::updateScript(QString gameScript, bool runAutoStartScripts)
{
    // 删除旧的 SessionContext
    if (m_sessionContext) {
        delete m_sessionContext;
        m_sessionContext = nullptr;
    }

    // 创建新的 SessionContext
    m_sessionContext = new SessionContext("default", this, this);

    if (!gameScript.isEmpty()) {
        m_sessionContext->loadKeyMap(gameScript, runAutoStartScripts);
    }

    // 设置分辨率
    if (m_mobileSize.isValid()) {
        m_sessionContext->setMobileSize(m_mobileSize);
    }

    // 重新设置帧获取回调
    if (m_frameGrabCallback) {
        m_sessionContext->setFrameGrabCallback(m_frameGrabCallback);
    }

    // 重新连接 tip 信号
    if (m_scriptTipCallback) {
        m_sessionContext->connectScriptTipSignal(m_scriptTipCallback);
    }

    // 重新连接键位覆盖层更新信号
    if (m_overlayUpdateCallback) {
        m_sessionContext->connectKeyMapOverlayUpdateSignal(m_overlayUpdateCallback);
    }

    // 连接光标抓取信号
    connect(m_sessionContext, &SessionContext::grabCursor, this, &Controller::grabCursor);
}

bool Controller::isCurrentCustomKeymap()
{
    return m_sessionContext ? m_sessionContext->isCurrentCustomKeymap() : false;
}

// ---------------------------------------------------------
// Android 功能快捷键实现
// ---------------------------------------------------------
void Controller::postBackOrScreenOn(bool down)
{
    if (down) {
        postKeyCodeClick(AKEYCODE_BACK);
    }
}

void Controller::postGoHome() { postKeyCodeClick(AKEYCODE_HOME); }
void Controller::postGoMenu() { postKeyCodeClick(AKEYCODE_MENU); }
void Controller::postGoBack() { postKeyCodeClick(AKEYCODE_BACK); }
void Controller::postAppSwitch() { postKeyCodeClick(AKEYCODE_APP_SWITCH); }
void Controller::postPower() { postKeyCodeClick(AKEYCODE_POWER); }
void Controller::postVolumeUp() { postKeyCodeClick(AKEYCODE_VOLUME_UP); }
void Controller::postVolumeDown() { postKeyCodeClick(AKEYCODE_VOLUME_DOWN); }

// ---------------------------------------------------------
// 输入事件转发（委托给 SessionContext）
// ---------------------------------------------------------
void Controller::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_sessionContext) m_sessionContext->mouseEvent(from, frameSize, showSize);
}

void Controller::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_sessionContext) m_sessionContext->wheelEvent(from, frameSize, showSize);
}

void Controller::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_sessionContext) m_sessionContext->keyEvent(from, frameSize, showSize);
}

// ---------------------------------------------------------
// 发送控制数据（非阻塞）
// ---------------------------------------------------------
bool Controller::sendControl(const QByteArray &buffer)
{
    if (buffer.isEmpty()) return false;

    if (!m_controlSender) {
        qWarning() << "[Controller] No sender available";
        return false;
    }

    return m_controlSender->send(buffer);
}

// 发送完整的按键点击动作 (按下 + 抬起)
void Controller::postKeyCodeClick(AndroidKeycode keycode)
{
    QByteArray data = FastMsg::keyClick(static_cast<quint16>(keycode));
    postFastMsg(data);
    }

void Controller::setMobileSize(const QSize &size)
{
    m_mobileSize = size;
    if (m_sessionContext) {
        m_sessionContext->setMobileSize(size);
    }
}

void Controller::setControlSocket(KcpControlSocket *socket)
{
    if (m_controlSender && socket) {
        m_controlSender->setSocket(socket);
        m_controlSender->setSendCallback(nullptr);
    }
}

void Controller::setTcpControlSocket(QTcpSocket *socket)
{
    if (m_controlSender && socket) {
        // TCP_NODELAY: 禁用 Nagle 算法，控制消息立即发出，不等待合并
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        // 缩小控制通道发送缓冲区
        socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 16 * 1024);
        m_controlSender->setTcpSocket(socket);
        m_controlSender->setSendCallback(nullptr);
    }
}

void Controller::setControlChannel(qsc::core::IControlChannel* channel)
{
    if (m_controlSender && channel) {
        m_controlSender->setControlChannel(channel);
        m_controlSender->setSendCallback(nullptr);
    }
}

void Controller::setFrameGrabCallback(std::function<QImage()> callback)
{
    m_frameGrabCallback = callback;

    if (m_sessionContext) {
        m_sessionContext->setFrameGrabCallback(callback);
    }
}

void Controller::connectScriptTipSignal(std::function<void(const QString&, int, int)> callback)
{
    m_scriptTipCallback = callback;

    if (m_sessionContext) {
        m_sessionContext->connectScriptTipSignal(callback);
    }
}

void Controller::connectKeyMapOverlayUpdateSignal(std::function<void()> callback)
{
    m_overlayUpdateCallback = callback;

    if (m_sessionContext) {
        m_sessionContext->connectKeyMapOverlayUpdateSignal(callback);
    }
}

void Controller::postDisconnect()
{
        if (m_controlSender) {
        m_controlSender->send(FastMsg::disconnect());
    }
    qInfo() << "[Controller] Sent disconnect message to server";
}

void Controller::onWindowFocusLost()
{
    if (m_sessionContext) {
        m_sessionContext->onWindowFocusLost();
    }
}

void Controller::resetScriptState()
{
    if (m_sessionContext) {
        m_sessionContext->resetScriptState();
    }
}

void Controller::runAutoStartScripts()
{
    if (m_sessionContext) {
        m_sessionContext->runAutoStartScripts();
    }
}

void Controller::resetAllTouchPoints()
{
    // 发送 FTA_RESET 命令到服务器，释放所有触摸点
    QByteArray data = FastMsg::serializeTouch(FastTouchEvent(0, FTA_RESET, 0, 0));
    postFastMsg(data);
}
