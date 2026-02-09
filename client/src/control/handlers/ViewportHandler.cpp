#define _USE_MATH_DEFINES
#include <cmath>
#include "ViewportHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "keymap.h"
#include "fastmsg.h"
#include "ConfigCenter.h"
#include <QDebug>
#include <QRandomGenerator>

// 静态辅助函数：应用随机偏移（与原版一致）
static QPointF applyRandomOffset(const QPointF& pos, const QSize& targetSize) {
    int offsetLevel = qsc::ConfigCenter::instance().randomOffset();
    if (offsetLevel <= 0 || targetSize.isEmpty()) {
        return pos;
    }

    double maxPixelOffset = offsetLevel * 0.5;

    double offsetX = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * maxPixelOffset;
    double offsetY = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * maxPixelOffset;

    double normalizedOffsetX = offsetX / targetSize.width();
    double normalizedOffsetY = offsetY / targetSize.height();

    QPointF result = pos;
    result.rx() += normalizedOffsetX;
    result.ry() += normalizedOffsetY;

    result.setX(qBound(0.001, result.x(), 0.999));
    result.setY(qBound(0.001, result.y(), 0.999));

    return result;
}

// 静态辅助函数：获取目标尺寸
static QSize getTargetSize(const QSize& frameSize, const QSize& showSize) {
    if (frameSize.isValid() && !frameSize.isEmpty()) {
        return frameSize;
    }
    return showSize;
}

ViewportHandler::ViewportHandler(QObject* parent)
    : IInputHandler(parent)
{
    // 初始化边缘回中定时器（与原版完全一致：15ms）
    m_state.centerRepressTimer = new QTimer(this);
    m_state.centerRepressTimer->setSingleShot(true);
    m_state.centerRepressTimer->setInterval(15);  // 15ms 延迟
    connect(m_state.centerRepressTimer, &QTimer::timeout, this, &ViewportHandler::onCenterRepressTimer);

    // 初始化空闲回中定时器（1000ms 空闲后回中）
    // 优化：回正后不会立即重启计时器，只有鼠标再次移动时才重启
    m_state.idleCenterTimer = new QTimer(this);
    m_state.idleCenterTimer->setSingleShot(true);
    m_state.idleCenterTimer->setInterval(1000);  // 1000ms 空闲后回中
    connect(m_state.idleCenterTimer, &QTimer::timeout, this, &ViewportHandler::onIdleCenterTimer);
}

ViewportHandler::~ViewportHandler()
{
    reset();
}

void ViewportHandler::init(Controller* controller, SessionContext* context)
{
    IInputHandler::init(controller, context);
}

bool ViewportHandler::handleKeyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
    return false;  // 视角控制不通过责任链处理键盘事件
}

bool ViewportHandler::handleMouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
    // 视角控制不通过责任链处理鼠标事件
    // 由 SessionContext::processMouseMove 直接调用本类的方法
    return false;
}

bool ViewportHandler::handleWheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
    return false;  // 视角控制不处理滚轮事件
}

void ViewportHandler::onFocusLost()
{
    reset();
}

void ViewportHandler::reset()
{
    stopTouch();
    m_pendingMoveDelta = {0, 0};
    m_moveSendScheduled = false;
}

void ViewportHandler::startTouch(const QSize& frameSize, const QSize& showSize)
{
    m_frameSize = frameSize;
    m_showSize = showSize;

    if (!m_state.touching && m_keyMap) {
        QPointF mouseMoveStartPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
        QSize targetSize = getTargetSize(m_frameSize, m_showSize);
        QPointF randomStartPos = applyRandomOffset(mouseMoveStartPos, targetSize);

        m_state.fastTouchSeqId = FastTouchSeq::next();
        sendFastTouch(FTA_DOWN, randomStartPos);
        m_state.lastConverPos = randomStartPos;
        m_state.touching = true;
    }
}

void ViewportHandler::addMoveDelta(const QPointF& delta)
{
    m_pendingMoveDelta += delta;
}

void ViewportHandler::scheduleMoveSend()
{
    if (!m_moveSendScheduled) {
        m_moveSendScheduled = true;
        // 直接在当前事件处理尾声发送（下一个 posted event 前）
        // 比 QTimer::singleShot(0) 省去一次完整的事件循环往返
        QMetaObject::invokeMethod(this, &ViewportHandler::onMouseMoveTimer, Qt::QueuedConnection);
    }
}

void ViewportHandler::accumulatePendingOvershoot(const QPointF& delta)
{
    m_state.pendingOvershoot += delta;
}

void ViewportHandler::stopTouch()
{
    // 停止边缘回中延迟定时器
    if (m_state.centerRepressTimer) {
        m_state.centerRepressTimer->stop();
    }
    m_state.waitingForCenterRepress = false;
    m_state.pendingOvershoot = {0, 0};

    // 停止空闲回中定时器并重置状态
    if (m_state.idleCenterTimer) {
        m_state.idleCenterTimer->stop();
    }
    m_state.idleCenterCompleted = false;

    if (m_state.touching) {
        sendFastTouch(FTA_UP, m_state.lastConverPos);
        m_state.touching = false;
        m_state.fastTouchSeqId = 0;
    }
}

void ViewportHandler::resetView()
{
    // 重置视角到中心
    stopTouch();
}

void ViewportHandler::onMouseMoveTimer()
{
    // 重置调度标记
    m_moveSendScheduled = false;

    // 如果正在等待边缘回中重按，暂时不处理移动（积累到 pendingOvershoot）
    if (m_state.waitingForCenterRepress) {
        m_state.pendingOvershoot += m_pendingMoveDelta;
        m_pendingMoveDelta = {0, 0};
        return;
    }

    // 如果没有移动量，直接返回
    if (m_pendingMoveDelta.isNull()) {
        return;
    }

    // 有新的移动：清除空闲回正完成标志，重新启动空闲计时器
    // 这样只有鼠标真正移动后才会重新开始计时
    m_state.idleCenterCompleted = false;
    if (m_state.idleCenterTimer) {
        m_state.idleCenterTimer->start();
    }

    processMove(m_pendingMoveDelta);
    m_pendingMoveDelta = {0, 0};
}

void ViewportHandler::processMove(const QPointF& delta)
{
    if (!m_keyMap) return;

    // 计算新位置
    QPointF newPos = m_state.lastConverPos + delta;

    QPointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
    const double MARGIN = 0.05;
    const double EDGE_MIN = MARGIN;
    const double EDGE_MAX = 1.0 - MARGIN;

    // 边界检测函数
    auto isOutOfBounds = [&](const QPointF& pos) {
        return pos.x() < EDGE_MIN || pos.x() > EDGE_MAX ||
               pos.y() < EDGE_MIN || pos.y() > EDGE_MAX;
    };

    // 边界处理：如果超出边界，使用边缘回中延迟定时器
    if (isOutOfBounds(newPos) && m_state.touching) {
        // 停止空闲定时器（边缘回中优先）
        if (m_state.idleCenterTimer) {
            m_state.idleCenterTimer->stop();
        }

        // Step 1: 移动到边缘
        QPointF edgePos;
        edgePos.setX(qBound(EDGE_MIN, newPos.x(), EDGE_MAX));
        edgePos.setY(qBound(EDGE_MIN, newPos.y(), EDGE_MAX));
        sendFastTouch(FTA_MOVE, edgePos);

        // Step 2: 在边缘抬起手指
        sendFastTouch(FTA_UP, edgePos);
        m_state.touching = false;

        // Step 3: 保存状态，启动延迟定时器
        m_state.waitingForCenterRepress = true;
        m_state.pendingCenterPos = centerPos;
        m_state.pendingOvershoot = newPos - edgePos;  // 超出边界的剩余增量
        m_state.centerRepressTimer->start();
        return;
    }

    // 正常情况：更新位置并发送
    m_state.lastConverPos = newPos;
    if (m_state.touching) {
        sendFastTouch(FTA_MOVE, m_state.lastConverPos);
    }
}

void ViewportHandler::onCenterRepressTimer()
{
    if (!m_state.waitingForCenterRepress || !m_keyMap) {
        return;
    }

    const double MARGIN = 0.05;
    const double EDGE_MIN = MARGIN;
    const double EDGE_MAX = 1.0 - MARGIN;

    // 边界检测函数
    auto isOutOfBounds = [&](const QPointF& pos) {
        return pos.x() < EDGE_MIN || pos.x() > EDGE_MAX ||
               pos.y() < EDGE_MIN || pos.y() > EDGE_MAX;
    };

    // 应用随机偏移到中心点
    QSize targetSize = getTargetSize(m_frameSize, m_showSize);
    QPointF randomCenterPos = applyRandomOffset(m_state.pendingCenterPos, targetSize);

    // Step 3: 在中心重新按下（生成新的 seqId）
    m_state.fastTouchSeqId = FastTouchSeq::next();
    sendFastTouch(FTA_DOWN, randomCenterPos);
    m_state.touching = true;

    // 计算新的中心位置（加上等待期间积累的增量）
    QPointF newCenterPos = randomCenterPos + m_state.pendingOvershoot;

    // 如果新位置仍然越界，clamp 到边界
    if (isOutOfBounds(newCenterPos)) {
        newCenterPos.setX(qBound(EDGE_MIN, newCenterPos.x(), EDGE_MAX));
        newCenterPos.setY(qBound(EDGE_MIN, newCenterPos.y(), EDGE_MAX));
    }

    // Step 4: 移动到新位置
    sendFastTouch(FTA_MOVE, newCenterPos);
    m_state.lastConverPos = newCenterPos;

    // 清除等待状态
    m_state.waitingForCenterRepress = false;
    m_state.pendingOvershoot = {0, 0};

    // 重新启动空闲定时器（仅当不是空闲回正完成的情况）
    // 如果是空闲回正完成的，等待鼠标移动后才重新启动
    if (m_state.idleCenterTimer && !m_state.idleCenterCompleted) {
        m_state.idleCenterTimer->start();
    }
}

void ViewportHandler::onIdleCenterTimer()
{
    // 如果正在等待边缘回中，不执行空闲回中
    if (m_state.waitingForCenterRepress || !m_keyMap) {
        return;
    }

    if (!m_state.touching) {
        return;
    }

    // 鼠标停止移动：使用边缘回中延迟定时器的方式回到中心
    QPointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;

    // Step 1: 在当前位置抬起手指
    sendFastTouch(FTA_UP, m_state.lastConverPos);
    m_state.touching = false;

    // Step 2: 标记空闲回正已完成，等待鼠标再次移动
    // 这样回正后不会立即重启计时器，只有鼠标移动后才重启
    m_state.idleCenterCompleted = true;

    // Step 3: 保存状态，启动延迟定时器（和边缘回中一样）
    m_state.waitingForCenterRepress = true;
    m_state.pendingCenterPos = centerPos;
    m_state.pendingOvershoot = {0, 0};  // 空闲回中没有超出增量
    m_state.centerRepressTimer->start();
}

void ViewportHandler::sendFastTouch(quint8 action, const QPointF& pos)
{
    if (!m_controller) return;

    quint16 nx = static_cast<quint16>(qBound(0.0, pos.x(), 1.0) * 65535);
    quint16 ny = static_cast<quint16>(qBound(0.0, pos.y(), 1.0) * 65535);

    // P-KCP: 栈上序列化，零堆分配
    char buf[10];
    FastTouchEvent evt(m_state.fastTouchSeqId, action, nx, ny);
    int len = FastMsg::serializeTouchInto(buf, evt);
    m_controller->postFastMsg(buf, len);
}
