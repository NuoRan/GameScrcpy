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

// 应用随机偏移
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
    m_state.centerRepressTimer.setInterval(0);
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
}

void ViewportHandler::startTouch(const Size& frameSize, const Size& showSize)
{
    m_frameSize = frameSize;
    m_showSize = showSize;

    if (!m_state.touching && m_keyMap) {
        PointF mouseMoveStartPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
        auto& mmStart = m_keyMap->getMouseMoveMap().data.mouseMove;
        double sMinX = 0.02, sMaxX = 0.98, sMinY = 0.02, sMaxY = 0.98;
        if (mmStart.areaMode) {
            sMinX = std::max(0.0, mmStart.areaX);
            sMinY = std::max(0.0, mmStart.areaY);
            sMaxX = std::min(1.0, mmStart.areaX + mmStart.areaW);
            sMaxY = std::min(1.0, mmStart.areaY + mmStart.areaH);
        }
        mouseMoveStartPos.x = std::clamp(mouseMoveStartPos.x, sMinX, sMaxX);
        mouseMoveStartPos.y = std::clamp(mouseMoveStartPos.y, sMinY, sMaxY);
        Size ms = m_sessionContext ? m_sessionContext->mobileSize() : Size();
        Size targetSize = getTargetSize(m_frameSize, m_showSize, ms);
        PointF randomStartPos = applyRandomOffset(mouseMoveStartPos, targetSize);

        m_state.fastTouchSeqId = FastTouchSeq::next();
        sendFastTouch(FTA_DOWN, randomStartPos);
        m_state.lastConverPos = randomStartPos;
        m_state.touching = true;
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
        m_state.pendingOvershoot = {0, 0};
        return;
    }

    if (!m_state.touching) return;

    PointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;
    auto& mm = m_keyMap->getMouseMoveMap().data.mouseMove;
    {
        double rMinX = 0.02, rMaxX = 0.98, rMinY = 0.02, rMaxY = 0.98;
        if (mm.areaMode) {
            rMinX = std::max(0.0, mm.areaX);
            rMinY = std::max(0.0, mm.areaY);
            rMaxX = std::min(1.0, mm.areaX + mm.areaW);
            rMaxY = std::min(1.0, mm.areaY + mm.areaH);
        }
        centerPos.x = std::clamp(centerPos.x, rMinX, rMaxX);
        centerPos.y = std::clamp(centerPos.y, rMinY, rMaxY);
    }
    double dx = m_state.lastConverPos.x - centerPos.x;
    double dy = m_state.lastConverPos.y - centerPos.y;
    if (std::sqrt(dx * dx + dy * dy) < 0.02) return;

    m_state.idleCenterTimer.stop();

    // 双指交替回中心：新手指先按下中心，旧手指再抬起
    Size ms = m_sessionContext ? m_sessionContext->mobileSize() : Size();
    Size targetSize = getTargetSize(m_frameSize, m_showSize, ms);
    PointF randomCenterPos = applyRandomOffset(centerPos, targetSize);
    uint32_t oldSeqId = m_state.fastTouchSeqId;
    PointF oldPos = m_state.lastConverPos;

    m_state.fastTouchSeqId = FastTouchSeq::next();
    sendFastTouch(FTA_DOWN, randomCenterPos);

    // 旧手指抬起
    {
        uint16_t ex = static_cast<uint16_t>(std::clamp(oldPos.x, 0.0, 1.0) * 65535);
        uint16_t ey = static_cast<uint16_t>(std::clamp(oldPos.y, 0.0, 1.0) * 65535);
        char buf[10];
        FastTouchEvent evt(oldSeqId, FTA_UP, ex, ey);
        int len = FastMsg::serializeTouchInto(buf, evt);
        m_controller->postFastMsg(buf, len);
    }

    m_state.lastConverPos = randomCenterPos;
    // touching 保持 true
}

void ViewportHandler::onMouseMoveTimer()
{
    m_moveSendScheduled = false;

    if (m_pendingMoveDelta.isNull()) return;

    m_state.idleCenterCompleted = false;
    m_state.idleCenterTimer.start();

    processMove(m_pendingMoveDelta);
    m_pendingMoveDelta = {0, 0};
}

void ViewportHandler::processMove(const PointF& delta)
{
    if (!m_keyMap) return;

    PointF newPos = m_state.lastConverPos + delta;

    PointF centerPos = m_keyMap->getMouseMoveMap().data.mouseMove.startPos;

    // 区域模式：使用自定义边界；否则使用全屏 0.02–0.98
    auto& mm = m_keyMap->getMouseMoveMap().data.mouseMove;
    double edgeMinX = 0.02, edgeMaxX = 0.98;
    double edgeMinY = 0.02, edgeMaxY = 0.98;
    if (mm.areaMode) {
        edgeMinX = std::max(0.0, mm.areaX);
        edgeMinY = std::max(0.0, mm.areaY);
        edgeMaxX = std::min(1.0, mm.areaX + mm.areaW);
        edgeMaxY = std::min(1.0, mm.areaY + mm.areaH);
    }
    // 始终确保 centerPos 在边界内，否则回正落在界外 → 立即又越界 → 死循环
    // 典型场景：centerPos 为 {0,0} 时回正到左上角，后续任何移动都 OOB
    centerPos.x = std::clamp(centerPos.x, edgeMinX, edgeMaxX);
    centerPos.y = std::clamp(centerPos.y, edgeMinY, edgeMaxY);

    bool oobX = (newPos.x < edgeMinX || newPos.x > edgeMaxX);
    bool oobY = (newPos.y < edgeMinY || newPos.y > edgeMaxY);

    if ((oobX || oobY) && m_state.touching) {
        // 双指交替回正：新手指先按下中心，旧手指再抬起
        // 游戏始终有一根手指在触摸，不会看到"所有手指抬起"的间隙

        // 1) 旧手指移动到钳制后的边缘位置
        PointF edgePos(std::clamp(newPos.x, edgeMinX, edgeMaxX),
                       std::clamp(newPos.y, edgeMinY, edgeMaxY));
        sendFastTouch(FTA_MOVE, edgePos);

        // 2) 新手指按下中心（此刻两根手指同时存在）
        Size ms = m_sessionContext ? m_sessionContext->mobileSize() : Size();
        Size targetSize = getTargetSize(m_frameSize, m_showSize, ms);
        PointF randomCenterPos = applyRandomOffset(centerPos, targetSize);
        uint32_t oldSeqId = m_state.fastTouchSeqId;
        m_state.fastTouchSeqId = FastTouchSeq::next();
        sendFastTouch(FTA_DOWN, randomCenterPos);

        // 3) 旧手指抬起（现在只剩新手指在中心）
        {
            uint16_t ex = static_cast<uint16_t>(std::clamp(edgePos.x, 0.0, 1.0) * 65535);
            uint16_t ey = static_cast<uint16_t>(std::clamp(edgePos.y, 0.0, 1.0) * 65535);
            char buf[10];
            FastTouchEvent evt(oldSeqId, FTA_UP, ex, ey);
            int len = FastMsg::serializeTouchInto(buf, evt);
            m_controller->postFastMsg(buf, len);
        }

        m_state.lastConverPos = randomCenterPos;
        // touching 保持 true，下一帧继续从中心移动
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

    // 将 overshoot 回灌并立即处理，不丢失任何移动量
    m_pendingMoveDelta += m_state.pendingOvershoot;
    m_state.pendingOvershoot = {0, 0};

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
    auto& mmIdle = m_keyMap->getMouseMoveMap().data.mouseMove;
    {
        double iMinX = 0.02, iMaxX = 0.98, iMinY = 0.02, iMaxY = 0.98;
        if (mmIdle.areaMode) {
            iMinX = std::max(0.0, mmIdle.areaX);
            iMinY = std::max(0.0, mmIdle.areaY);
            iMaxX = std::min(1.0, mmIdle.areaX + mmIdle.areaW);
            iMaxY = std::min(1.0, mmIdle.areaY + mmIdle.areaH);
        }
        centerPos.x = std::clamp(centerPos.x, iMinX, iMaxX);
        centerPos.y = std::clamp(centerPos.y, iMinY, iMaxY);
    }

    // 双指交替空闲回正：新手指先按下中心，旧手指再抬起
    Size ms = m_sessionContext ? m_sessionContext->mobileSize() : Size();
    Size targetSize = getTargetSize(m_frameSize, m_showSize, ms);
    PointF randomCenterPos = applyRandomOffset(centerPos, targetSize);
    uint32_t oldSeqId = m_state.fastTouchSeqId;
    PointF oldPos = m_state.lastConverPos;

    m_state.fastTouchSeqId = FastTouchSeq::next();
    sendFastTouch(FTA_DOWN, randomCenterPos);

    // 旧手指抬起
    {
        uint16_t ex = static_cast<uint16_t>(std::clamp(oldPos.x, 0.0, 1.0) * 65535);
        uint16_t ey = static_cast<uint16_t>(std::clamp(oldPos.y, 0.0, 1.0) * 65535);
        char buf[10];
        FastTouchEvent evt(oldSeqId, FTA_UP, ex, ey);
        int len = FastMsg::serializeTouchInto(buf, evt);
        m_controller->postFastMsg(buf, len);
    }

    m_state.lastConverPos = randomCenterPos;
    m_state.idleCenterCompleted = true;
    // touching 保持 true
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
