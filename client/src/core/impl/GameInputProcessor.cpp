#include "GameInputProcessor.h"
#include "controller.h"
#include "SessionContext.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

namespace qsc {
namespace core {

GameInputProcessor::GameInputProcessor(Controller* controller)
    : m_controller(controller)
{
    // SessionContext 由 Controller 内部创建和管理
    if (m_controller) {
        m_sessionContext = m_controller->sessionContext();
    }
}

GameInputProcessor::~GameInputProcessor() = default;

void GameInputProcessor::processKeyEvent(const QKeyEvent* event,
                                         const QSize& frameSize,
                                         const QSize& showSize)
{
    if (m_sessionContext) {
        m_sessionContext->keyEvent(event, frameSize, showSize);
    }
}

void GameInputProcessor::processMouseEvent(const QMouseEvent* event,
                                           const QSize& frameSize,
                                           const QSize& showSize)
{
    if (m_sessionContext) {
        m_sessionContext->mouseEvent(event, frameSize, showSize);
    }
}

void GameInputProcessor::processWheelEvent(const QWheelEvent* event,
                                           const QSize& frameSize,
                                           const QSize& showSize)
{
    if (m_sessionContext) {
        m_sessionContext->wheelEvent(event, frameSize, showSize);
    }
}

void GameInputProcessor::loadKeyMap(const QString& json, bool runAutoStart)
{
    if (m_sessionContext) {
        m_sessionContext->loadKeyMap(json, runAutoStart);
    }
}

void GameInputProcessor::onWindowFocusLost()
{
    if (m_sessionContext) {
        m_sessionContext->onWindowFocusLost();
    }
}

void GameInputProcessor::resetState()
{
    if (m_sessionContext) {
        m_sessionContext->resetScriptState();
    }
}

void GameInputProcessor::releaseAllTouchPoints()
{
    // 通过 onWindowFocusLost 间接实现
    if (m_sessionContext) {
        m_sessionContext->onWindowFocusLost();
    }
}

void GameInputProcessor::setTouchCallback(TouchCallback callback)
{
    m_touchCallback = std::move(callback);
    // SessionContext 直接通过 Controller 发送，不使用回调
}

void GameInputProcessor::setKeyCallback(KeyCallback callback)
{
    m_keyCallback = std::move(callback);
    // SessionContext 直接通过 Controller 发送，不使用回调
}

void GameInputProcessor::setCursorGrabCallback(CursorGrabCallback callback)
{
    m_cursorGrabCallback = std::move(callback);
    // TODO: 连接 SessionContext 的光标状态变化信号
}

void GameInputProcessor::setFrameGrabCallback(FrameGrabCallback callback)
{
    if (m_sessionContext) {
        // 将 void* 回调转换为 QImage 回调
        m_sessionContext->setFrameGrabCallback([callback]() -> QImage {
            if (callback) {
                void* result = callback();
                if (result) {
                    return *static_cast<QImage*>(result);
                }
            }
            return QImage();
        });
    }
}

void GameInputProcessor::setScriptTipCallback(ScriptTipCallback callback)
{
    if (m_sessionContext) {
        m_sessionContext->connectScriptTipSignal(
            [callback](const QString& msg, int durationMs, int keyId) {
                if (callback) {
                    callback(msg, durationMs, keyId);
                }
            }
        );
    }
}

void GameInputProcessor::setKeyMapOverlayCallback(KeyMapOverlayCallback callback)
{
    if (m_sessionContext) {
        m_sessionContext->connectKeyMapOverlayUpdateSignal(
            [callback]() {
                if (callback) {
                    callback();
                }
            }
        );
    }
}

void GameInputProcessor::runAutoStartScripts()
{
    if (m_sessionContext) {
        m_sessionContext->runAutoStartScripts();
    }
}

void GameInputProcessor::resetScriptState()
{
    if (m_sessionContext) {
        m_sessionContext->resetScriptState();
    }
}

bool GameInputProcessor::isGameMode() const
{
    // SessionContext 总是游戏模式
    return true;
}

} // namespace core
} // namespace qsc
