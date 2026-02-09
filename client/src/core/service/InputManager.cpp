#include "InputManager.h"
#include "controller.h"
#include "interfaces/IControlChannel.h"
#include "kcpcontrolsocket.h"
#include "keycodes.h"
#include <QTcpSocket>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPointer>

namespace qsc {
namespace core {

InputManager::InputManager(QObject* parent)
    : QObject(parent)
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

void InputManager::initialize(KcpSendCallback sendCallback, const QString& gameScript)
{
    m_controller = std::make_unique<Controller>(std::move(sendCallback), gameScript);

    connect(m_controller.get(), &Controller::grabCursor, this, &InputManager::cursorGrabChanged);

    // 使用 QPointer 保护 this，防止 callback 被调用时 InputManager 已销毁
    QPointer<InputManager> safeThis = this;

    m_controller->connectScriptTipSignal([safeThis](const QString& msg, int durationMs, int keyId) {
        if (safeThis) {
            emit safeThis->scriptTip(msg, durationMs, keyId);
        }
    });

    m_controller->connectKeyMapOverlayUpdateSignal([safeThis]() {
        if (safeThis) {
            emit safeThis->keyMapOverlayUpdated();
        }
    });
}

void InputManager::setControlChannel(IControlChannel* channel)
{
    m_controlChannel = channel;
    // 【新架构】将 IControlChannel 传递给 Controller
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

void InputManager::setTcpControlSocket(QTcpSocket* socket)
{
    if (m_controller) {
        m_controller->setTcpControlSocket(socket);
    }
}

void InputManager::setMobileSize(const QSize& size)
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

void InputManager::keyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize)
{
    if (m_controller) {
        m_controller->keyEvent(event, frameSize, showSize);
    }
}

void InputManager::mouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize)
{
    if (m_controller) {
        m_controller->mouseEvent(event, frameSize, showSize);
    }
}

void InputManager::wheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize)
{
    if (m_controller) {
        m_controller->wheelEvent(event, frameSize, showSize);
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

void InputManager::updateScript(const QString& gameScript, bool runAutoStartScripts)
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

void InputManager::setFrameGrabCallback(std::function<QImage()> callback)
{
    if (m_controller) {
        m_controller->setFrameGrabCallback(std::move(callback));
    }
}

} // namespace core
} // namespace qsc
