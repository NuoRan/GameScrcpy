#define LOG_TAG "Controller"
#include "Logger.h"
#include "InputEvent.h"

#include "controller.h"
#include "controlsender.h"
#include "NativeTcpSocket.h"
#include "SessionContext.h"
#include "kcpcontrolsocket.h"
#include "fastmsg.h"
#include "interfaces/IControlChannel.h"
#include "StringUtils.h"

// 初始化 SessionContext 和异步发送器
Controller::Controller(KcpSendCallback sendCallback, std::string gameScript)
    : m_sendCallback(sendCallback)
{
    // 创建异步发送器
    m_controlSender = new ControlSender();
    m_controlSender->setSendCallback(sendCallback);

    // 连接异步发送器的信号
    m_controlSender->sendError.connect([](const std::string &error) {
        LOG_W("[Controller] Send error: %s", error.c_str());
    });

    updateScript(std::move(gameScript));
}

Controller::~Controller()
{
    stopSender();

    // 删除 SessionContext
    if (m_sessionContext) {
        delete m_sessionContext;
        m_sessionContext = nullptr;
    }

    // 删除 ControlSender (Controller 不再是 QObject，不会自动清理)
    delete m_controlSender;
    m_controlSender = nullptr;
}

void Controller::startSender()
{
    if (m_senderStarted) {
        return;
    }
    if (m_controlSender) {
        m_controlSender->start();
        m_senderStarted = true;
        m_disconnectSent = false;
    }
}

void Controller::stopSender()
{
    if (!m_senderStarted) {
        return;
    }
    // 先发送断开消息通知服务端
    postDisconnect();

    if (m_controlSender) {
        m_controlSender->stop();
    }
    m_senderStarted = false;
}

void Controller::postFastMsg(const std::vector<uint8_t> &data)
{
    if (data.empty()) return;
    if (m_controlSender) {
        m_controlSender->send(data);
    }
}

void Controller::postFastMsg(const char *data, int len)
{
    if (!data || len <= 0) return;
    if (m_controlSender) {
        m_controlSender->send(data, len);
    }
}

void Controller::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    (void)deviceMsg;
}

// ---------------------------------------------------------
// 更新键位映射脚本
// ---------------------------------------------------------
void Controller::updateScript(std::string gameScript, bool runAutoStartScripts)
{
    if (m_sessionContext) {
        // 复用已有的 SessionContext，避免在 Demuxer 线程活跃时销毁对象导致崩溃
        if (!gameScript.empty()) {
            m_sessionContext->loadKeyMap(gameScript, runAutoStartScripts);
        } else {
            m_sessionContext->resetScriptState();
        }

        // 刷新分辨率（可能在两次调用之间发生了变化）
        if (m_mobileSize.isValid()) {
            m_sessionContext->setMobileSize(m_mobileSize);
        }
        return;
    }

    // 首次调用：创建新的 SessionContext
    m_sessionContext = new SessionContext("default", this);

    if (!gameScript.empty()) {
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
    if (m_grayFrameGrabCallback) {
        m_sessionContext->setGrayFrameGrabCallback(m_grayFrameGrabCallback);
    }

    // 重新连接 tip 信号
    if (m_scriptTipCallback) {
        auto cb = m_scriptTipCallback;
        m_sessionContext->connectScriptTipSignal([cb](const std::string& msg, int d, int k) {
            cb(msg, d, k);
        });
    }

    // 重新连接键位覆盖层更新信号
    if (m_overlayUpdateCallback) {
        m_sessionContext->connectKeyMapOverlayUpdateSignal(m_overlayUpdateCallback);
    }

    // 连接光标抓取信号
    m_sessionContext->grabCursor.connect([this](bool grab) {
        grabCursor.fire(grab);
    });
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
void Controller::mouseEvent(const InputEvent& from, const Size &frameSize, const Size &showSize)
{
    if (m_sessionContext) m_sessionContext->mouseEvent(from, frameSize, showSize);
}

void Controller::wheelEvent(const InputEvent& from, const Size &frameSize, const Size &showSize)
{
    if (m_sessionContext) m_sessionContext->wheelEvent(from, frameSize, showSize);
}

void Controller::keyEvent(const InputEvent& from, const Size &frameSize, const Size &showSize)
{
    if (m_sessionContext) m_sessionContext->keyEvent(from, frameSize, showSize);
}

// 发送完整的按键点击动作 (按下 + 抬起)
void Controller::postKeyCodeClick(AndroidKeycode keycode)
{
    auto data = FastMsg::keyClick(static_cast<uint16_t>(keycode));
    postFastMsg(data);
    }

void Controller::setMobileSize(const Size &size)
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

void Controller::setTcpControlSocket(NativeTcpSocket *socket)
{
    if (m_controlSender && socket) {
        // TCP_NODELAY: disable Nagle, send control messages immediately
        socket->setNoDelay(true);
        // Shrink send buffer to reduce kernel queuing latency
        socket->setSendBufferSize(16 * 1024);
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

void Controller::setFrameGrabCallback(std::function<cv::Mat()> callback)
{
    m_frameGrabCallback = callback;

    if (m_sessionContext) {
        m_sessionContext->setFrameGrabCallback(callback);
    }
}

void Controller::setGrayFrameGrabCallback(GrayFrameGrabCallback callback)
{
    m_grayFrameGrabCallback = callback;

    if (m_sessionContext) {
        m_sessionContext->setGrayFrameGrabCallback(callback);
    }
}

void Controller::connectScriptTipSignal(std::function<void(const std::string&, int, int)> callback)
{
    m_scriptTipCallback = callback;

    if (m_sessionContext) {
        // SessionContext now uses std::string directly
        m_sessionContext->connectScriptTipSignal([callback](const std::string& msg, int d, int k) {
            if (callback) callback(msg, d, k);
        });
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
    if (m_disconnectSent) {
        return;
    }
    if (m_controlSender && m_senderStarted) {
        m_controlSender->send(FastMsg::disconnect());
        m_disconnectSent = true;
        LOGI() << "[Controller] Sent disconnect message to server";
    }
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
    char buf[6];
    int len = FastMsg::serializeTouchInto(buf, FastTouchEvent(0, FTA_RESET, 0, 0));
    postFastMsg(buf, len);
}

void Controller::postSetVideoBitRate(uint32_t bitrate)
{
    auto data = FastMsg::setVideoBitRate(bitrate);
    postFastMsg(data);
}

void Controller::postSetDisplayPower(bool on)
{
    auto data = FastMsg::setDisplayPower(on);
    postFastMsg(data);
}
