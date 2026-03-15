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

static Size getTargetSize(const Size& frameSize, const Size& showSize, const Size& mobileSize) {
    if (mobileSize.isValid() && !mobileSize.isEmpty()) return mobileSize;
    if (frameSize.isValid() && !frameSize.isEmpty()) return frameSize;
    return showSize;
}

ViewportHandler::ViewportHandler()
{
    m_state.centerRepressTimer.setSingleShot(true);
    m_state.centerRepressTimer.setInterval(20);
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
}

void ViewportHandler::startTouch(const Size& frameSize, const Size& showSize)
{
    m_frameSize = frameSize;
    m_showSize = showSize;

    if (!m_state.touching && m_keyMap) {
        PointF mouseMoveStartPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
        Size ms = m_sessionContext ? m_sessionContext->mobileSize() : Size();
        Size targetSize = getTargetSize(m_frameSize, m_showSize, ms);
        PointF randomStartPos = applyRandomOffset(mouseMoveStartPos, targetSize);

        m_state.fastTouchSeqId = FastTouchSeq::next();
        sendFastTouch(FTA_DOWN, randomStartPos);
        m_state.lastConverPos = randomStartPos;
        m_state.touching = true;
        m_smoothedDelta = {0, 0};
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

    if (m_state.waitingForCenterRepress) {
        // 已在回中过程中：清除累计的 overshoot / 动量，确保落点在真正中心
        m_state.pendingOvershoot = {0, 0};
        m_smoothedDelta = {0, 0};
        return;
    }

    if (!m_state.touching) return;

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

    // 自适应 EMA：低速强平滑（消锯齿），高速弱平滑（不拖沓）
    double speed = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    double t = std::clamp((speed - SPEED_LOW) / (SPEED_HIGH - SPEED_LOW), 0.0, 1.0);
    double factor = FACTOR_LOW + t * (FACTOR_HIGH - FACTOR_LOW);

    m_smoothedDelta.x = factor * delta.x + (1.0 - factor) * m_smoothedDelta.x;
    m_smoothedDelta.y = factor * delta.y + (1.0 - factor) * m_smoothedDelta.y;

    PointF newPos = m_state.lastConverPos + m_smoothedDelta;

    PointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
    constexpr double EDGE_MIN = 0.02, EDGE_MAX = 0.98;

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

    Size ms = m_sessionContext ? m_sessionContext->mobileSize() : Size();
    Size targetSize = getTargetSize(m_frameSize, m_showSize, ms);
    PointF randomCenterPos = applyRandomOffset(m_state.pendingCenterPos, targetSize);

    m_state.fastTouchSeqId = FastTouchSeq::next();

    sendFastTouch(FTA_DOWN, randomCenterPos);
    m_state.touching = true;
    m_state.lastConverPos = randomCenterPos;

    m_state.waitingForCenterRepress = false;

    // 将 overshoot 回灌到正常管线，由自适应 EMA 平滑消化
    // 不再一次性 MOVE，避免单帧大跳步
    m_pendingMoveDelta += m_state.pendingOvershoot;
    m_state.pendingOvershoot = {0, 0};
    m_smoothedDelta = {0, 0}; // 全新触摸序列，清空 EMA 历史

    if (!m_pendingMoveDelta.isNull()) {
        scheduleMoveSend();
    }

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
