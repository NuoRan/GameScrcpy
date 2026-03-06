#include "FreeLookHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "ViewportHandler.h"
#include "keymap.h"
#include "fastmsg.h"
#define LOG_TAG "FreeLookHandler"
#include "Logger.h"

// 静态辅助函数：获取目标尺寸
static Size getTargetSize(const Size& frameSize, const Size& showSize) {
    if (frameSize.isValid() && !frameSize.isEmpty()) {
        return frameSize;
    }
    return showSize;
}

FreeLookHandler::FreeLookHandler()
{
}

FreeLookHandler::~FreeLookHandler()
{
    reset();
}

void FreeLookHandler::init(Controller* controller, SessionContext* context)
{
    IInputHandler::init(controller, context);
}

bool FreeLookHandler::handleKeyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    // 责任链调用：需要自己查找 node
    // 注意：这里无法获取修饰键信息，所以可能找不到正确的配置
    // 主要入口应该是 processKeyEvent（由 SessionContext::processFreeLook 调用）
    if (!m_keyMap) return false;

    int key = event.key;
    const KeyMap::KeyMapNode& node = m_keyMap->getKeyMapNodeKey(key);

    // 只处理小眼睛类型
    if (node.type != KeyMap::KMT_FREE_LOOK) {
        return false;
    }

    // 委托给 processKeyEvent 处理
    processKeyEvent(node, event, frameSize, showSize);
    return true;
}

void FreeLookHandler::processKeyEvent(const KeyMap::KeyMapNode& node, const InputEvent& event,
                                      const Size& frameSize, const Size& showSize)
{
    m_frameSize = frameSize;
    m_showSize = showSize;

    int key = event.key;
    bool isModifier = (key == GameKey::Key_Alt || key == GameKey::Key_Shift ||
                       key == GameKey::Key_Control || key == GameKey::Key_Meta);

    bool isPress = (event.type == InputEventType::KeyPress);
    bool isRelease = (event.type == InputEventType::KeyRelease);

    // 对于修饰键作为热键，检测是否是组合键
    if (isModifier && isPress && m_modifierComboDetected) {
        return;  // 检测到组合键，不触发
    }

    if (isPress && !m_state.active) {
        // 按下热键：启动自由视角（独立的触摸点，不影响视角控制）
        m_state.active = true;
        m_state.triggerKey = node.data.freeLook.keyNode.key;
        m_state.startPos = node.data.freeLook.startPos;
        m_state.speedRatio = node.data.freeLook.speedRatio;
        m_state.currentPos = node.data.freeLook.startPos;
        m_state.resetViewOnRelease = node.data.freeLook.resetViewOnRelease;

        // 生成新的序列 ID 并发送 DOWN
        m_state.fastTouchSeqId = FastTouchSeq::next();
        sendFastTouch(FTA_DOWN, m_state.startPos);
    }
    else if (isRelease && m_state.active) {
        // 松开热键：结束自由视角
        // 对于修饰键，如果检测到组合键，也必须发送 UP 释放触摸点
        // 否则服务端触摸点永久残留，导致"卡键"
        if (isModifier && m_modifierComboDetected) {
            // 组合键误触发，但必须释放已按下的触摸点
            if (m_state.fastTouchSeqId != 0) {
                sendFastTouch(FTA_UP, m_state.currentPos);
            }
            m_state.active = false;
            m_state.fastTouchSeqId = 0;
            m_state.resetViewOnRelease = false;
            m_modifierComboDetected = false;
            m_lastModifierKey = 0;
            return;
        }

        sendFastTouch(FTA_UP, m_state.currentPos);

        // 如果配置了松开时重置视角，调用 ViewportHandler::resetView
        if (m_state.resetViewOnRelease && m_sessionContext) {
            ViewportHandler* viewportHandler = m_sessionContext->viewportHandler();
            if (viewportHandler) {
                viewportHandler->resetView();
            }
        }

        m_state.active = false;
        m_state.fastTouchSeqId = 0;
        m_state.resetViewOnRelease = false;

        // 重置组合键检测状态
        if (isModifier) {
            m_modifierComboDetected = false;
            m_lastModifierKey = 0;
        }
    }
}

bool FreeLookHandler::handleMouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event;
    (void)frameSize;
    (void)showSize;
    // 鼠标移动处理由 processMouseDelta 完成，不通过责任链
    return false;
}

bool FreeLookHandler::handleWheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event;
    (void)frameSize;
    (void)showSize;
    return false;
}

void FreeLookHandler::onFocusLost()
{
    reset();
}

void FreeLookHandler::reset()
{
    if (m_state.active && m_state.fastTouchSeqId != 0) {
        sendFastTouch(FTA_UP, m_state.currentPos);
    }

    m_state.active = false;
    m_state.triggerKey = GameKey::Key_unknown;
    m_state.fastTouchSeqId = 0;
    m_modifierComboDetected = false;
    m_lastModifierKey = 0;
}

void FreeLookHandler::processMouseDelta(const PointF& delta, const Size& frameSize, const Size& showSize)
{
    if (!m_state.active || m_state.fastTouchSeqId == 0) {
        return;
    }

    m_frameSize = frameSize;
    m_showSize = showSize;

    PointF speedRatio = m_state.speedRatio;
    Size targetSize = getTargetSize(m_frameSize, m_showSize);

    if (targetSize.width <= 0 || targetSize.height <= 0 ||
        speedRatio.x <= 0 || speedRatio.y <= 0) {
        return;
    }

    PointF distance;
    distance.x = delta.x / speedRatio.x / targetSize.width;
    distance.y = delta.y / speedRatio.y / targetSize.height;

    // 计算新位置
    PointF newPos = m_state.currentPos + distance;

    // 简单 clamp 到屏幕边界，不做回中处理
    newPos.x = std::clamp(newPos.x, 0.0, 1.0);
    newPos.y = std::clamp(newPos.y, 0.0, 1.0);

    // 发送移动事件
    sendFastTouch(FTA_MOVE, newPos);
    m_state.currentPos = newPos;
}

void FreeLookHandler::sendFastTouch(uint8_t action, const PointF& pos)
{
    if (!m_controller) return;

    uint16_t nx = static_cast<uint16_t>(std::clamp(pos.x, 0.0, 1.0) * 65535);
    uint16_t ny = static_cast<uint16_t>(std::clamp(pos.y, 0.0, 1.0) * 65535);

    // 栈缓冲区序列化，避免堆分配
    char buf[10];
    FastTouchEvent evt(m_state.fastTouchSeqId, action, nx, ny);
    int len = FastMsg::serializeTouchInto(buf, evt);
    m_controller->postFastMsg(buf, len);
}
