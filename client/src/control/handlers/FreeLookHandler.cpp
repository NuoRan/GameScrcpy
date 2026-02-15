#include "FreeLookHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "ViewportHandler.h"
#include "keymap.h"
#include "fastmsg.h"
#include <QDebug>

// 静态辅助函数：获取目标尺寸
static QSize getTargetSize(const QSize& frameSize, const QSize& showSize) {
    if (frameSize.isValid() && !frameSize.isEmpty()) {
        return frameSize;
    }
    return showSize;
}

FreeLookHandler::FreeLookHandler(QObject* parent)
    : IInputHandler(parent)
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

bool FreeLookHandler::handleKeyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize)
{
    // 责任链调用：需要自己查找 node
    // 注意：这里无法获取修饰键信息，所以可能找不到正确的配置
    // 主要入口应该是 processKeyEvent（由 SessionContext::processFreeLook 调用）
    if (!m_keyMap) return false;

    int key = event->key();
    const KeyMap::KeyMapNode& node = m_keyMap->getKeyMapNodeKey(key);

    // 只处理小眼睛类型
    if (node.type != KeyMap::KMT_FREE_LOOK) {
        return false;
    }

    // 委托给 processKeyEvent 处理
    processKeyEvent(node, event, frameSize, showSize);
    return true;
}

void FreeLookHandler::processKeyEvent(const KeyMap::KeyMapNode& node, const QKeyEvent* event,
                                      const QSize& frameSize, const QSize& showSize)
{
    if (!event) return;

    m_frameSize = frameSize;
    m_showSize = showSize;

    int key = event->key();
    bool isModifier = (key == Qt::Key_Alt || key == Qt::Key_Shift ||
                       key == Qt::Key_Control || key == Qt::Key_Meta);

    bool isPress = (event->type() == QEvent::KeyPress);
    bool isRelease = (event->type() == QEvent::KeyRelease);

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
        // 对于修饰键，如果检测到组合键，取消操作
        if (isModifier && m_modifierComboDetected) {
            // 组合键情况下直接取消，不发送 UP
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

bool FreeLookHandler::handleMouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
    // 鼠标移动处理由 processMouseDelta 完成，不通过责任链
    return false;
}

bool FreeLookHandler::handleWheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
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
    m_state.triggerKey = Qt::Key_unknown;
    m_state.fastTouchSeqId = 0;
    m_modifierComboDetected = false;
    m_lastModifierKey = 0;
}

void FreeLookHandler::processMouseDelta(const QPointF& delta, const QSize& frameSize, const QSize& showSize)
{
    if (!m_state.active || m_state.fastTouchSeqId == 0) {
        return;
    }

    m_frameSize = frameSize;
    m_showSize = showSize;

    QPointF speedRatio = m_state.speedRatio;
    QSize targetSize = getTargetSize(m_frameSize, m_showSize);

    if (targetSize.width() <= 0 || targetSize.height() <= 0 ||
        speedRatio.x() <= 0 || speedRatio.y() <= 0) {
        return;
    }

    QPointF distance;
    distance.setX(delta.x() / speedRatio.x() / targetSize.width());
    distance.setY(delta.y() / speedRatio.y() / targetSize.height());

    // 计算新位置
    QPointF newPos = m_state.currentPos + distance;

    // 简单 clamp 到屏幕边界，不做回中处理
    newPos.setX(qBound(0.0, newPos.x(), 1.0));
    newPos.setY(qBound(0.0, newPos.y(), 1.0));

    // 发送移动事件
    sendFastTouch(FTA_MOVE, newPos);
    m_state.currentPos = newPos;
}

void FreeLookHandler::sendFastTouch(quint8 action, const QPointF& pos)
{
    if (!m_controller) return;

    quint16 nx = static_cast<quint16>(qBound(0.0, pos.x(), 1.0) * 65535);
    quint16 ny = static_cast<quint16>(qBound(0.0, pos.y(), 1.0) * 65535);

    // 栈缓冲区序列化，避免堆分配
    char buf[10];
    FastTouchEvent evt(m_state.fastTouchSeqId, action, nx, ny);
    int len = FastMsg::serializeTouchInto(buf, evt);
    m_controller->postFastMsg(buf, len);
}
