#define _USE_MATH_DEFINES
#include <cmath>
#include "ViewportHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "keymap.h"
#include "fastmsg.h"
#include "ConfigCenter.h"
#include <QRandomGenerator>
#include <algorithm>

// 应用随机偏移（防检测）
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

static QSize getTargetSize(const QSize& frameSize, const QSize& showSize) {
    if (frameSize.isValid() && !frameSize.isEmpty()) {
        return frameSize;
    }
    return showSize;
}

ViewportHandler::ViewportHandler(QObject* parent)
    : IInputHandler(parent)
{
    m_state.centerRepressTimer = new QTimer(this);
    m_state.centerRepressTimer->setSingleShot(true);
    m_state.centerRepressTimer->setInterval(5);
    connect(m_state.centerRepressTimer, &QTimer::timeout, this, &ViewportHandler::onCenterRepressTimer);

    m_state.idleCenterTimer = new QTimer(this);
    m_state.idleCenterTimer->setSingleShot(true);
    m_state.idleCenterTimer->setInterval(1000);
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
    Q_UNUSED(event) Q_UNUSED(frameSize) Q_UNUSED(showSize)
    return false;
}

bool ViewportHandler::handleMouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event) Q_UNUSED(frameSize) Q_UNUSED(showSize)
    return false;
}

bool ViewportHandler::handleWheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event) Q_UNUSED(frameSize) Q_UNUSED(showSize)
    return false;
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
    m_smoothedDelta = {0, 0};
    m_subPixelAccum = {0, 0};
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
        m_smoothedDelta = {0, 0};
        m_subPixelAccum = {0, 0};
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
        onMouseMoveTimer();
    }
}

void ViewportHandler::accumulatePendingOvershoot(const QPointF& delta)
{
    m_state.pendingOvershoot += delta;
}

void ViewportHandler::stopTouch()
{
    if (m_state.centerRepressTimer)
        m_state.centerRepressTimer->stop();
    m_state.waitingForCenterRepress = false;
    m_state.pendingOvershoot = {0, 0};

    if (m_state.idleCenterTimer)
        m_state.idleCenterTimer->stop();
    m_state.idleCenterCompleted = false;

    if (m_state.touching) {
        sendFastTouch(FTA_UP, m_state.lastConverPos);
        m_state.touching = false;
        m_state.fastTouchSeqId = 0;
    }
}

void ViewportHandler::resetView()
{
    if (!m_keyMap) return;
    if (m_state.waitingForCenterRepress || !m_state.touching) return;

    // 已在中心附近则跳过，防止多个异步 resetView 产生无意义微触摸导致跳屏
    QPointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
    double dx = m_state.lastConverPos.x() - centerPos.x();
    double dy = m_state.lastConverPos.y() - centerPos.y();
    if (std::sqrt(dx * dx + dy * dy) < 0.02) return;

    if (m_state.idleCenterTimer)
        m_state.idleCenterTimer->stop();

    sendFastTouch(FTA_UP, m_state.lastConverPos);
    m_state.touching = false;

    // 5ms 快速回中（与边缘回中同机制）
    m_state.waitingForCenterRepress = true;
    m_state.pendingCenterPos = centerPos;
    m_state.pendingOvershoot = {0, 0};
    m_state.centerRepressTimer->start();

    m_smoothedDelta = {0, 0};
    m_subPixelAccum = {0, 0};
}


void ViewportHandler::onMouseMoveTimer()
{
    m_moveSendScheduled = false;

    if (m_state.waitingForCenterRepress) {
        m_state.pendingOvershoot += m_pendingMoveDelta;
        m_pendingMoveDelta = {0, 0};
        return;
    }

    if (m_pendingMoveDelta.isNull()) return;

    m_state.idleCenterCompleted = false;
    if (m_state.idleCenterTimer)
        m_state.idleCenterTimer->start();

    processMove(m_pendingMoveDelta);
    m_pendingMoveDelta = {0, 0};
}

void ViewportHandler::processMove(const QPointF& delta)
{
    if (!m_keyMap) return;

    // 管线: 亚像素累积 → 抖动过滤 → EMA 平滑 → 边界处理
    QPointF rawDelta = delta + m_subPixelAccum;
    double magnitude = std::sqrt(rawDelta.x() * rawDelta.x() + rawDelta.y() * rawDelta.y());
    if (magnitude < JITTER_THRESHOLD) {
        m_subPixelAccum = rawDelta;
        return;
    }
    m_subPixelAccum = {0, 0};

    m_smoothedDelta.setX(SMOOTH_FACTOR * rawDelta.x() + (1.0 - SMOOTH_FACTOR) * m_smoothedDelta.x());
    m_smoothedDelta.setY(SMOOTH_FACTOR * rawDelta.y() + (1.0 - SMOOTH_FACTOR) * m_smoothedDelta.y());

    QPointF newPos = m_state.lastConverPos + m_smoothedDelta;

    QPointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
    constexpr double EDGE_MIN = 0.05, EDGE_MAX = 0.95;

    auto isOutOfBounds = [](const QPointF& p) {
        return p.x() < EDGE_MIN || p.x() > EDGE_MAX || p.y() < EDGE_MIN || p.y() > EDGE_MAX;
    };

    if (isOutOfBounds(newPos) && m_state.touching) {
        if (m_state.idleCenterTimer)
            m_state.idleCenterTimer->stop();

        QPointF edgePos(qBound(EDGE_MIN, newPos.x(), EDGE_MAX), qBound(EDGE_MIN, newPos.y(), EDGE_MAX));
        sendFastTouch(FTA_MOVE, edgePos);
        sendFastTouch(FTA_UP, edgePos);
        m_state.touching = false;

        m_state.waitingForCenterRepress = true;
        m_state.pendingCenterPos = centerPos;
        m_state.pendingOvershoot = newPos - edgePos;
        m_state.centerRepressTimer->start();
        return;
    }
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

    constexpr double EDGE_MIN = 0.05, EDGE_MAX = 0.95;

    QSize targetSize = getTargetSize(m_frameSize, m_showSize);
    QPointF randomCenterPos = applyRandomOffset(m_state.pendingCenterPos, targetSize);

    m_state.fastTouchSeqId = FastTouchSeq::next();

    // 限制 overshoot 幅度，防止回中后第一帧跳屏
    constexpr double MAX_OVERSHOOT = 0.005;
    double overshootMag = std::sqrt(m_state.pendingOvershoot.x() * m_state.pendingOvershoot.x() +
                                    m_state.pendingOvershoot.y() * m_state.pendingOvershoot.y());
    if (overshootMag > MAX_OVERSHOOT)
        m_state.pendingOvershoot *= MAX_OVERSHOOT / overshootMag;

    sendFastTouch(FTA_DOWN, randomCenterPos);
    m_state.touching = true;

    QPointF newCenterPos = randomCenterPos + m_state.pendingOvershoot;
    newCenterPos.setX(qBound(EDGE_MIN, newCenterPos.x(), EDGE_MAX));
    newCenterPos.setY(qBound(EDGE_MIN, newCenterPos.y(), EDGE_MAX));

    sendFastTouch(FTA_MOVE, newCenterPos);
    m_state.lastConverPos = newCenterPos;

    m_state.waitingForCenterRepress = false;
    m_state.pendingOvershoot = {0, 0};
    m_smoothedDelta = {0, 0};
    m_subPixelAccum = {0, 0};

    if (m_state.idleCenterTimer && !m_state.idleCenterCompleted)
        m_state.idleCenterTimer->start();
}

void ViewportHandler::onIdleCenterTimer()
{
    if (m_state.waitingForCenterRepress || !m_keyMap || !m_state.touching) return;

    QPointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;

    sendFastTouch(FTA_UP, m_state.lastConverPos);
    m_state.touching = false;
    m_state.idleCenterCompleted = true;

    m_state.waitingForCenterRepress = true;
    m_state.pendingCenterPos = centerPos;
    m_state.pendingOvershoot = {0, 0};
    m_state.centerRepressTimer->start();
}

void ViewportHandler::sendFastTouch(quint8 action, const QPointF& pos)
{
    if (!m_controller) return;

    quint16 nx = static_cast<quint16>(qBound(0.0, pos.x(), 1.0) * 65535);
    quint16 ny = static_cast<quint16>(qBound(0.0, pos.y(), 1.0) * 65535);

    char buf[10];
    FastTouchEvent evt(m_state.fastTouchSeqId, action, nx, ny);
    int len = FastMsg::serializeTouchInto(buf, evt);
    m_controller->postFastMsg(buf, len);
}
