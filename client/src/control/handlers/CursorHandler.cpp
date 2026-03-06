#include "CursorHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "fastmsg.h"
#define LOG_TAG "CursorHandler"
#include "Logger.h"

CursorHandler::CursorHandler()
{
}

CursorHandler::~CursorHandler()
{
    reset();
}

void CursorHandler::init(Controller* controller, SessionContext* context)
{
    IInputHandler::init(controller, context);
}

bool CursorHandler::handleKeyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event;
    (void)frameSize;
    (void)showSize;
    // 光标模式不处理键盘事件
    return false;
}

bool CursorHandler::handleMouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)frameSize;
    // 责任链调用：不通过责任链，由 SessionContext 直接调用 processMouseEvent
    (void)event;
    (void)showSize;
    return false;
}

bool CursorHandler::handleWheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event;
    (void)frameSize;
    (void)showSize;
    return false;
}

void CursorHandler::onFocusLost()
{
    reset();
}

void CursorHandler::reset()
{
    // 如果正在触摸，发送 UP 事件
    if (m_state.touching && m_state.fastTouchSeqId != 0) {
        sendFastTouch(FTA_UP, m_state.lastPos);
    }

    m_state.touching = false;
    m_state.fastTouchSeqId = 0;
}

void CursorHandler::processMouseEvent(const InputEvent& event, const Size& showSize)
{
    if (showSize.width <= 0 || showSize.height <= 0) return;

    m_showSize = showSize;

    PointF localPos(event.localX, event.localY);

    // 计算归一化坐标 (0.0 - 1.0)
    PointF normalizedPos(localPos.x / m_showSize.width, localPos.y / m_showSize.height);

    // 始终更新光标位置（用于 getmousepos API）
    m_state.lastPos = normalizedPos;

    // 光标显示模式：使用 FastMsg 协议发送触摸，只响应左键的实际触摸

    // 过滤：只处理左键事件
    if (event.type == InputEventType::MousePress || event.type == InputEventType::MouseRelease) {
        if (event.button != InputButton::Left) {
            return;  // 只处理左键按下/释放
        }
    }
    if (event.type == InputEventType::MouseMove) {
        if (!(event.buttons & InputButton::Left)) {
            return;  // 只处理左键按住时的移动
        }
    }

    switch (event.type) {
    case InputEventType::MousePress:
        // 按下：生成新序列 ID 并发送 DOWN
        m_state.fastTouchSeqId = FastTouchSeq::next();
        m_state.touching = true;
        sendFastTouch(FTA_DOWN, normalizedPos);
        break;

    case InputEventType::MouseRelease:
        // 释放：发送 UP 并重置状态
        if (m_state.touching) {
            sendFastTouch(FTA_UP, normalizedPos);
            m_state.touching = false;
            m_state.fastTouchSeqId = 0;
        }
        break;

    case InputEventType::MouseMove:
        // 移动：发送 MOVE（仅在按下时）
        if (m_state.touching) {
            sendFastTouch(FTA_MOVE, normalizedPos);
        }
        break;

    default:
        break;
    }
}

void CursorHandler::sendFastTouch(uint8_t action, const PointF& pos)
{
    if (!m_controller) return;

    uint16_t nx = static_cast<uint16_t>(std::clamp(pos.x, 0.0, 1.0) * 65535);
    uint16_t ny = static_cast<uint16_t>(std::clamp(pos.y, 0.0, 1.0) * 65535);

    char buf[10];
    FastTouchEvent evt(m_state.fastTouchSeqId, action, nx, ny);
    int len = FastMsg::serializeTouchInto(buf, evt);
    m_controller->postFastMsg(buf, len);
}
