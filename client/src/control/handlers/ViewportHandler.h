#ifndef VIEWPORTHANDLER_H
#define VIEWPORTHANDLER_H

#include "IInputHandler.h"
#include "NativeTimer.h"
#include "GameTypes.h"


class Controller;
class KeyMap;
class SessionContext;

/**
 * @brief 视角控制处理器 / Viewport Control Handler
 *
 * 处理鼠标移动以控制游戏视角，包括 / Handles mouse movement for game viewport control:
 * - 鼠标移动转换为触摸滑动 / Mouse movement to touch swipe conversion
 * - 边缘回中机制 / Edge re-centering mechanism
 * - 空闲回中机制 / Idle re-centering mechanism
 * - 随机偏移（防检测）/ Random offset (anti-detection)
 */
class ViewportHandler : public IInputHandler
{
public:
    explicit ViewportHandler();
    ~ViewportHandler();

    void init(Controller* controller, SessionContext* context) override;

    // IInputHandler interface - 视角控制不通过责任链处理，返回 false
    bool handleKeyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    bool handleMouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    bool handleWheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    void onFocusLost() override;
    void reset() override;
    int priority() const override { return 80; }
    std::string name() const override { return "ViewportHandler"; }

    // 设置 KeyMap 引用
    void setKeyMap(KeyMap* keyMap) { m_keyMap = keyMap; }

    // ========== 视角控制核心接口（由 SessionContext 调用）==========

    // 开始/停止触摸
    void startTouch(const Size& frameSize, const Size& showSize);
    void stopTouch();
    bool isTouching() const { return m_state.touching; }

    // 处理移动增量（由 SessionContext::processMouseMove 调用）
    void addMoveDelta(const PointF& delta);
    void scheduleMoveSend();

    // 供 FreeLook 检查是否正在等待回中
    bool isWaitingForCenterRepress() const { return m_state.waitingForCenterRepress; }

    // 累积待处理的增量到 pendingOvershoot（边缘回中等待期间）
    void accumulatePendingOvershoot(const PointF& delta);

    // 获取当前触摸位置
    PointF lastConvertedPos() const { return m_state.lastConverPos; }

    // 重置视角到中心（脚本调用）
    void resetView();

private:
    void onMouseMoveTimer();
    void onCenterRepressTimer();
    void onIdleCenterTimer();

private:
    void sendFastTouch(uint8_t action, const PointF& pos);
    void processMove(const PointF& delta);

    // 注意：m_controller 和 m_sessionContext 继承自 IInputHandler
    KeyMap* m_keyMap = nullptr;

    Size m_frameSize;
    Size m_showSize;

    // 视角控制状态
    struct {
        PointF lastConverPos;
        PointF lastPos = {0.0, 0.0};
        bool touching = false;
        uint32_t fastTouchSeqId = 0;

        // 边缘回中延迟状态
        bool waitingForCenterRepress = false;
        PointF pendingCenterPos;
        PointF pendingOvershoot;
        NativeTimer centerRepressTimer;

        // 空闲回中定时器
        NativeTimer idleCenterTimer;
        bool idleCenterCompleted = false;  // 空闲回正已完成，等待鼠标移动
    } m_state;

    // 鼠标移动发送
    PointF m_pendingMoveDelta;
    bool m_moveSendScheduled = false;

    // ========== 视角平滑控制 ==========
    // 设计原则：原始灵敏度完全不变，只叠加平滑
    //  - EMA 平滑：消除微抖动，保持丝滑
    //  - 亚像素累积：微小位移不丢失

    // 平滑参数
    static constexpr double SMOOTH_FACTOR = 0.85;          // EMA 平滑系数 (0.85=轻微平滑, 1.0=无平滑)
    static constexpr double JITTER_THRESHOLD = 0.00008;    // 抖动过滤阈值（极小，仅过滤传感器噪声）

    PointF m_smoothedDelta = {0, 0};   // EMA 平滑状态
    PointF m_subPixelAccum = {0, 0};   // 亚像素精度累积
};

#endif // VIEWPORTHANDLER_H
