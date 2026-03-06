#include "GameInputProcessor.h"
#include "controller.h"
#include "SessionContext.h"
#include "StringUtils.h"

namespace qsc {
namespace core {

GameInputProcessor::GameInputProcessor(Controller* controller)
    : m_controller(controller)
{
    // SessionContext 由 Controller 内部创建和管理
    // 不再缓存指针，因为 Controller::updateScript() 会销毁并重建 SessionContext
    // 所有访问都通过 m_controller->sessionContext() 获取最新指针
}

GameInputProcessor::~GameInputProcessor() = default;

SessionContext* GameInputProcessor::sessionContext() const
{
    return currentContext();
}

SessionContext* GameInputProcessor::currentContext() const
{
    return m_controller ? m_controller->sessionContext() : nullptr;
}

void GameInputProcessor::processKeyEvent(const InputEvent& event,
                                         const Size& frameSize,
                                         const Size& showSize)
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->keyEvent(event, frameSize, showSize);
    }
}

void GameInputProcessor::processMouseEvent(const InputEvent& event,
                                           const Size& frameSize,
                                           const Size& showSize)
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->mouseEvent(event, frameSize, showSize);
    }
}

void GameInputProcessor::processWheelEvent(const InputEvent& event,
                                           const Size& frameSize,
                                           const Size& showSize)
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->wheelEvent(event, frameSize, showSize);
    }
}

void GameInputProcessor::loadKeyMap(const std::string& json, bool runAutoStart)
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->loadKeyMap(json, runAutoStart);
    }
}

void GameInputProcessor::onWindowFocusLost()
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->onWindowFocusLost();
    }
}

void GameInputProcessor::resetState()
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->resetScriptState();
    }
}

void GameInputProcessor::releaseAllTouchPoints()
{
    // 通过 onWindowFocusLost 间接实现
    auto* ctx = currentContext();
    if (ctx) {
        ctx->onWindowFocusLost();
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
    auto* ctx = currentContext();
    if (ctx) {
        // FrameGrabCallback 已经是 std::function<cv::Mat()>，直接转发给 SessionContext
        ctx->setFrameGrabCallback(std::move(callback));
    }
}

void GameInputProcessor::setScriptTipCallback(ScriptTipCallback callback)
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->connectScriptTipSignal(
            [callback](const std::string& msg, int durationMs, int keyId) {
                if (callback) {
                    callback(msg, durationMs, keyId);
                }
            }
        );
    }
}

void GameInputProcessor::setKeyMapOverlayCallback(KeyMapOverlayCallback callback)
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->connectKeyMapOverlayUpdateSignal(
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
    auto* ctx = currentContext();
    if (ctx) {
        ctx->runAutoStartScripts();
    }
}

void GameInputProcessor::resetScriptState()
{
    auto* ctx = currentContext();
    if (ctx) {
        ctx->resetScriptState();
    }
}

bool GameInputProcessor::isGameMode() const
{
    // SessionContext 总是游戏模式
    return true;
}

} // namespace core
} // namespace qsc
