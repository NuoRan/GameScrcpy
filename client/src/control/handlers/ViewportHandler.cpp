#define _USE_MATH_DEFINES
#include <cmath>
#include "ViewportHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "keymap.h"
#include "fastmsg.h"
#include "ConfigCenter.h"
#include <random>
#include <algorithm>

static thread_local std::mt19937 t_rng{std::random_device{}()};

// 应用随机偏移（防检测）
static PointF applyRandomOffset(const PointF& pos, const Size& targetSize) {
    int offsetLevel = qsc::ConfigCenter::instance().randomOffset();
    if (offsetLevel <= 0 || targetSize.isEmpty()) {
        return pos;
    }

    double maxPixelOffset = offsetLevel * 0.5;

    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double offsetX = (dist(t_rng) - 0.5) * 2.0 * maxPixelOffset;
    double offsetY = (dist(t_rng) - 0.5) * 2.0 * maxPixelOffset;

    double normalizedOffsetX = offsetX / targetSize.width;
    double normalizedOffsetY = offsetY / targetSize.height;

    PointF result = pos;
    result.x += normalizedOffsetX;
    result.y += normalizedOffsetY;

    result.x = std::clamp(result.x, 0.001, 0.999);
    result.y = std::clamp(result.y, 0.001, 0.999);

    return result;
}

static Size getTargetSize(const Size& frameSize, const Size& showSize) {
    if (frameSize.isValid() && !frameSize.isEmpty()) {
        return frameSize;
    }
    return showSize;
}

ViewportHandler::ViewportHandler()
{
    m_state.centerRepressTimer.setSingleShot(true);
    m_state.centerRepressTimer.setInterval(5);
    m_state.centerRepressTimer.setCallback([this]() { onCenterRepressTimer(); });

    m_state.idleCenterTimer.setSingleShot(true);
    m_state.idleCenterTimer.setInterval(1000);
    m_state.idleCenterTimer.setCallback([this]() { onIdleCenterTimer(); });
}

ViewportHandler::~ViewportHandler()
{
    reset();
}

void ViewportHandler::init(Controller* controller, SessionContext* context)
{
    IInputHandler::init(controller, context);
}

bool ViewportHandler::handleKeyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event; (void)frameSize; (void)showSize;
    return false;
}

bool ViewportHandler::handleMouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event; (void)frameSize; (void)showSize;
    return false;
}

bool ViewportHandler::handleWheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event; (void)frameSize; (void)showSize;
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

void ViewportHandler::startTouch(const Size& frameSize, const Size& showSize)
{
    m_frameSize = frameSize;
    m_showSize = showSize;

    if (!m_state.touching && m_keyMap) {
        PointF mouseMoveStartPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
        Size targetSize = getTargetSize(m_frameSize, m_showSize);
        PointF randomStartPos = applyRandomOffset(mouseMoveStartPos, targetSize);

        m_state.fastTouchSeqId = FastTouchSeq::next();
        sendFastTouch(FTA_DOWN, randomStartPos);
        m_state.lastConverPos = randomStartPos;
        m_state.touching = true;
        m_smoothedDelta = {0, 0};
        m_subPixelAccum = {0, 0};
    }
}

void ViewportHandler::addMoveDelta(const PointF& delta)
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

void ViewportHandler::accumulatePendingOvershoot(const PointF& delta)
{
    m_state.pendingOvershoot += delta;
}

void ViewportHandler::stopTouch()
{
    m_state.centerRepressTimer.stop();
    m_state.waitingForCenterRepress = false;
    m_state.pendingOvershoot = {0, 0};

    m_state.idleCenterTimer.stop();
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
    PointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
    double dx = m_state.lastConverPos.x - centerPos.x;
    double dy = m_state.lastConverPos.y - centerPos.y;
    if (std::sqrt(dx * dx + dy * dy) < 0.02) return;

    m_state.idleCenterTimer.stop();

    sendFastTouch(FTA_UP, m_state.lastConverPos);
    m_state.touching = false;

    // 5ms 快速回中（与边缘回中同机制）
    m_state.waitingForCenterRepress = true;
    m_state.pendingCenterPos = centerPos;
    m_state.pendingOvershoot = {0, 0};
    m_state.centerRepressTimer.start();

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
    m_state.idleCenterTimer.start();

    processMove(m_pendingMoveDelta);
    m_pendingMoveDelta = {0, 0};
}

void ViewportHandler::processMove(const PointF& delta)
{
    if (!m_keyMap) return;

    // 管线: 亚像素累积 → 抖动过滤 → EMA 平滑 → 边界处理
    PointF rawDelta = delta + m_subPixelAccum;
    double magnitude = std::sqrt(rawDelta.x * rawDelta.x + rawDelta.y * rawDelta.y);
    if (magnitude < JITTER_THRESHOLD) {
        m_subPixelAccum = rawDelta;
        return;
    }
    m_subPixelAccum = {0, 0};

    m_smoothedDelta.x = SMOOTH_FACTOR * rawDelta.x + (1.0 - SMOOTH_FACTOR) * m_smoothedDelta.x;
    m_smoothedDelta.y = SMOOTH_FACTOR * rawDelta.y + (1.0 - SMOOTH_FACTOR) * m_smoothedDelta.y;

    PointF newPos = m_state.lastConverPos + m_smoothedDelta;

    PointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
    constexpr double EDGE_MIN = 0.05, EDGE_MAX = 0.95;

    auto isOutOfBounds = [](const PointF& p) {
        return p.x < EDGE_MIN || p.x > EDGE_MAX || p.y < EDGE_MIN || p.y > EDGE_MAX;
    };

    if (isOutOfBounds(newPos) && m_state.touching) {
        m_state.idleCenterTimer.stop();

        PointF edgePos(std::clamp(newPos.x, EDGE_MIN, EDGE_MAX), std::clamp(newPos.y, EDGE_MIN, EDGE_MAX));
        sendFastTouch(FTA_MOVE, edgePos);
        sendFastTouch(FTA_UP, edgePos);
        m_state.touching = false;

        m_state.waitingForCenterRepress = true;
        m_state.pendingCenterPos = centerPos;
        m_state.pendingOvershoot = newPos - edgePos;
        m_state.centerRepressTimer.start();
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

    Size targetSize = getTargetSize(m_frameSize, m_showSize);
    PointF randomCenterPos = applyRandomOffset(m_state.pendingCenterPos, targetSize);

    m_state.fastTouchSeqId = FastTouchSeq::next();

    // 限制 overshoot 幅度，防止回中后第一帧跳屏
    constexpr double MAX_OVERSHOOT = 0.005;
    double overshootMag = std::sqrt(m_state.pendingOvershoot.x * m_state.pendingOvershoot.x +
                                    m_state.pendingOvershoot.y * m_state.pendingOvershoot.y);
    if (overshootMag > MAX_OVERSHOOT)
        m_state.pendingOvershoot *= MAX_OVERSHOOT / overshootMag;

    sendFastTouch(FTA_DOWN, randomCenterPos);
    m_state.touching = true;

    PointF newCenterPos = randomCenterPos + m_state.pendingOvershoot;
    newCenterPos.x = std::clamp(newCenterPos.x, EDGE_MIN, EDGE_MAX);
    newCenterPos.y = std::clamp(newCenterPos.y, EDGE_MIN, EDGE_MAX);

    sendFastTouch(FTA_MOVE, newCenterPos);
    m_state.lastConverPos = newCenterPos;

    m_state.waitingForCenterRepress = false;
    m_state.pendingOvershoot = {0, 0};
    m_smoothedDelta = {0, 0};
    m_subPixelAccum = {0, 0};

    if (!m_state.idleCenterCompleted)
        m_state.idleCenterTimer.start();
}

void ViewportHandler::onIdleCenterTimer()
{
    if (m_state.waitingForCenterRepress || !m_keyMap || !m_state.touching) return;

    PointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;

    sendFastTouch(FTA_UP, m_state.lastConverPos);
    m_state.touching = false;
    m_state.idleCenterCompleted = true;

    m_state.waitingForCenterRepress = true;
    m_state.pendingCenterPos = centerPos;
    m_state.pendingOvershoot = {0, 0};
    m_state.centerRepressTimer.start();
}

void ViewportHandler::sendFastTouch(uint8_t action, const PointF& pos)
{
    if (!m_controller) return;

    uint16_t nx = static_cast<uint16_t>(std::clamp(pos.x, 0.0, 1.0) * 65535);
    uint16_t ny = static_cast<uint16_t>(std::clamp(pos.y, 0.0, 1.0) * 65535);

    char buf[10];
    FastTouchEvent evt(m_state.fastTouchSeqId, action, nx, ny);
    int len = FastMsg::serializeTouchInto(buf, evt);
    m_controller->postFastMsg(buf, len);
}
