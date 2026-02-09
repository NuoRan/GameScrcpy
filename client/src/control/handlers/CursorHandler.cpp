#include "CursorHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "fastmsg.h"
#include <QDebug>

CursorHandler::CursorHandler(QObject* parent)
    : IInputHandler(parent)
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

bool CursorHandler::handleKeyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
    // 光标模式不处理键盘事件
    return false;
}

bool CursorHandler::handleMouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(frameSize)
    // 责任链调用：不通过责任链，由 SessionContext 直接调用 processMouseEvent
    Q_UNUSED(event)
    Q_UNUSED(showSize)
    return false;
}

bool CursorHandler::handleWheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
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

void CursorHandler::processMouseEvent(const QMouseEvent* event, const QSize& showSize)
{
    if (!event) return;
    if (showSize.width() <= 0 || showSize.height() <= 0) return;

    m_showSize = showSize;

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPointF localPos = event->localPos();
#else
    QPointF localPos = event->position();
#endif

    // 计算归一化坐标 (0.0 - 1.0)
    QPointF normalizedPos(localPos.x() / m_showSize.width(), localPos.y() / m_showSize.height());

    // 始终更新光标位置（用于 getmousepos API）
    m_state.lastPos = normalizedPos;

    // 【光标显示模式】使用 FastMsg 协议发送触摸事件，避免与轮盘/宏等冲突
    // 只响应左键的实际触摸（因为 Android 只识别一个主触摸点）
    // 中键和右键的事件被"吃掉"，不触发任何操作

    // 过滤：只处理左键事件
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        if (event->button() != Qt::LeftButton) {
            return;  // 只处理左键按下/释放
        }
    }
    if (event->type() == QEvent::MouseMove) {
        if (!(event->buttons() & Qt::LeftButton)) {
            return;  // 只处理左键按住时的移动
        }
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
        // 按下：生成新序列 ID 并发送 DOWN
        m_state.fastTouchSeqId = FastTouchSeq::next();
        m_state.touching = true;
        sendFastTouch(FTA_DOWN, normalizedPos);
        break;

    case QEvent::MouseButtonRelease:
        // 释放：发送 UP 并重置状态
        if (m_state.touching) {
            sendFastTouch(FTA_UP, normalizedPos);
            m_state.touching = false;
            m_state.fastTouchSeqId = 0;
        }
        break;

    case QEvent::MouseMove:
        // 移动：发送 MOVE（仅在按下时）
        if (m_state.touching) {
            sendFastTouch(FTA_MOVE, normalizedPos);
        }
        break;

    default:
        break;
    }
}

void CursorHandler::sendFastTouch(quint8 action, const QPointF& pos)
{
    if (!m_controller) return;

    quint16 nx = static_cast<quint16>(qBound(0.0, pos.x(), 1.0) * 65535);
    quint16 ny = static_cast<quint16>(qBound(0.0, pos.y(), 1.0) * 65535);

    QByteArray data;
    if (action == FTA_DOWN) {
        data = FastMsg::touchDownRaw(m_state.fastTouchSeqId, nx, ny);
    } else if (action == FTA_UP) {
        data = FastMsg::touchUpRaw(m_state.fastTouchSeqId, nx, ny);
    } else {
        data = FastMsg::touchMoveRaw(m_state.fastTouchSeqId, nx, ny);
    }

    m_controller->postFastMsg(data);
}
