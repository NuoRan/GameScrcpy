#include "InputManager.h"
#include "controller.h"
#include "SessionContext.h"
#include "interfaces/IControlChannel.h"
#include "kcpcontrolsocket.h"
#include "keycodes.h"
#include "StringUtils.h"

namespace qsc {
namespace core {

InputManager::InputManager()
{
}

InputManager::~InputManager()
{
    // 清除回调，防止销毁后被调用
    if (m_controller) {
        m_controller->connectScriptTipSignal(nullptr);
        m_controller->connectKeyMapOverlayUpdateSignal(nullptr);
    }
    stop();
}

void InputManager::initialize(KcpSendCallback sendCallback, const std::string& gameScript)
{
    m_controller = std::make_unique<Controller>(std::move(sendCallback), gameScript);

    // Controller::grabCursor 是 Signal<> — 直接 connect
    m_controller->grabCursor.connect([this](bool grabbed) {
        if (m_cursorGrabCb) m_cursorGrabCb(grabbed);
    });

    m_controller->connectScriptTipSignal([this](const std::string& msg, int durationMs, int keyId) {
        if (m_scriptTipCb) m_scriptTipCb(msg, durationMs, keyId);
    });

    m_controller->connectKeyMapOverlayUpdateSignal([this]() {
        if (m_keyMapOverlayCb) m_keyMapOverlayCb();
    });
}

void InputManager::setControlChannel(IControlChannel* channel)
{
    m_controlChannel = channel;
    // 将 IControlChannel 传递给 Controller
    if (m_controller) {
        m_controller->setControlChannel(channel);
    }
}

void InputManager::setKcpControlSocket(KcpControlSocket* socket)
{
    if (m_controller) {
        m_controller->setControlSocket(socket);
    }
}

void InputManager::setTcpControlSocket(NativeTcpSocket* socket)
{
    if (m_controller) {
        m_controller->setTcpControlSocket(socket);
    }
}

void InputManager::setMobileSize(const Size& size)
{
    m_mobileSize = size;
    if (m_controller) {
        m_controller->setMobileSize(size);
    }
}

void InputManager::start()
{
    if (m_controller) {
        m_controller->startSender();
    }
}

void InputManager::stop()
{
    if (m_controller) {
        m_controller->stopSender();
    }
}

// === 事件处理 ===

void InputManager::keyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    if (m_controller) {
        m_controller->keyEvent(event, frameSize, showSize);
    }
}

void InputManager::mouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    if (m_controller) {
        m_controller->mouseEvent(event, frameSize, showSize);
    }
}

void InputManager::wheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    if (m_controller) {
        m_controller->wheelEvent(event, frameSize, showSize);
    }
}

void InputManager::setDevicePixelRatio(double dpr)
{
    if (m_controller && m_controller->sessionContext()) {
        m_controller->sessionContext()->setDevicePixelRatio(dpr);
    }
}

// === 系统命令 ===

void InputManager::postGoBack()
{
    if (m_controller) {
        m_controller->postGoBack();
    }
}

void InputManager::postGoHome()
{
    if (m_controller) {
        m_controller->postGoHome();
    }
}

void InputManager::postGoMenu()
{
    if (m_controller) {
        m_controller->postGoMenu();
    }
}

void InputManager::postAppSwitch()
{
    if (m_controller) {
        m_controller->postAppSwitch();
    }
}

void InputManager::postPower()
{
    if (m_controller) {
        m_controller->postPower();
    }
}

void InputManager::postVolumeUp()
{
    if (m_controller) {
        m_controller->postVolumeUp();
    }
}

void InputManager::postVolumeDown()
{
    if (m_controller) {
        m_controller->postVolumeDown();
    }
}

void InputManager::postBackOrScreenOn(bool down)
{
    if (m_controller) {
        m_controller->postBackOrScreenOn(down);
    }
}

void InputManager::postKeyCodeClick(int keycode)
{
    if (m_controller) {
        m_controller->postKeyCodeClick(static_cast<AndroidKeycode>(keycode));
    }
}

void InputManager::postDisconnect()
{
    if (m_controller) {
        m_controller->postDisconnect();
    }
}

void InputManager::postSetVideoBitRate(uint32_t bitrate)
{
    if (m_controller) {
        m_controller->postSetVideoBitRate(bitrate);
    }
}

void InputManager::postSetDisplayPower(bool on)
{
    if (m_controller) {
        m_controller->postSetDisplayPower(on);
    }
}

// === 状态管理 ===

void InputManager::onWindowFocusLost()
{
    if (m_controller) {
        m_controller->onWindowFocusLost();
    }
}

void InputManager::resetAllTouchPoints()
{
    if (m_controller) {
        m_controller->resetAllTouchPoints();
    }
}

// === 脚本管理 ===

void InputManager::updateScript(const std::string& gameScript, bool runAutoStartScripts)
{
    if (m_controller) {
        m_controller->updateScript(gameScript, runAutoStartScripts);
    }
}

void InputManager::resetScriptState()
{
    if (m_controller) {
        m_controller->resetScriptState();
    }
}

void InputManager::runAutoStartScripts()
{
    if (m_controller) {
        m_controller->runAutoStartScripts();
    }
}

bool InputManager::isCurrentCustomKeymap() const
{
    return m_controller ? m_controller->isCurrentCustomKeymap() : false;
}

// === 帧获取 ===

void InputManager::setFrameGrabCallback(std::function<cv::Mat()> callback)
{
    if (m_controller) {
        m_controller->setFrameGrabCallback(std::move(callback));
    }
}

void InputManager::setGrayFrameGrabCallback(GrayFrameGrabCallback callback)
{
    if (m_controller) {
        m_controller->setGrayFrameGrabCallback(std::move(callback));
    }
}

} // namespace core
} // namespace qsc
