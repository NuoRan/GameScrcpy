#ifndef STEERWHEELHANDLER_H
#define STEERWHEELHANDLER_H

#include "IInputHandler.h"
#include "NativeTimer.h"
#include "keymap.h"
#include <deque>

/**
 * @brief 方向盘（WASD轮盘）处理器 / Steer Wheel (WASD) Handler
 *
 * 职责 / Responsibilities：
 * - 处理 WASD 按键输入 / Handle WASD key input
 * - 计算轮盘触摸位置 / Calculate wheel touch position
 * - 发送 FastMsg 触摸事件 / Send FastMsg touch events
 * - 支持拟人化波动 / Support human-like fluctuation
 * - 支持组合键延迟检测 / Support combo key delay detection
 */
class SteerWheelHandler : public IInputHandler
{
public:
    explicit SteerWheelHandler();
    ~SteerWheelHandler() override;

    void init(Controller* controller, SessionContext* context) override;
    bool handleKeyEvent(const InputEvent& event,
                        const Size& frameSize,
                        const Size& showSize) override;
    void onFocusLost() override;
    void reset() override;
    std::string name() const override { return "SteerWheelHandler"; }
    int priority() const override { return 20; }  // 高优先级

    // ========== 配置方法 ==========

    /**
     * @brief 设置 KeyMap 引用（用于获取轮盘配置）
     */
    void setKeyMap(KeyMap* keyMap) { m_keyMap = keyMap; }

    /**
     * @brief 设置轮盘系数（脚本 API 调用）
     */
    void setCoefficient(double up, double down, double left, double right);

    /**
     * @brief 重置轮盘系数为默认值
     */
    void resetCoefficient();

    /**
     * @brief 重置轮盘状态（场景切换时调用）
     */
    void resetWheel();

private:
    void onSteerWheelTimer();
    void onFirstPressTimer();
    void onHumanizeTimer();
    void onResetRepressTimer();

private:
    void processSteerWheel(const KeyMap::KeyMapNode& node, const InputEvent& event);
    void executeMove(const KeyMap::KeyMapNode& node);
    void sendFastTouch(uint8_t action, const PointF& pos);
    void getDelayQueue(const PointF& start, const PointF& end,
                       double distanceStep, double posStep,
                       uint32_t lowestTimer, uint32_t highestTimer,
                       std::deque<PointF>& queuePos, uint32_t& stepTimerMs);

    KeyMap* m_keyMap = nullptr;
    Size m_frameSize;
    Size m_showSize;

    // 轮盘状态
    struct {
        int touchKey = GameKey::Key_unknown;
        bool pressedUp = false;
        bool pressedDown = false;
        bool pressedLeft = false;
        bool pressedRight = false;
        uint32_t fastTouchSeqId = 0;
        bool isFirstPress = true;
        NativeTimer firstPressTimer;
        const KeyMap::KeyMapNode* pendingNode = nullptr;

        // 拟人化参数
        double currentAngleOffset = 0.0;
        double currentLengthFactor = 1.0;
        double targetAngleOffset = 0.0;
        double targetLengthFactor = 1.0;
        NativeTimer humanizeTimer;
        int lastPressedState = 0;

        // 延迟数据
        struct {
            PointF currentPos;
            NativeTimer timer;
            std::deque<PointF> queuePos;
            uint32_t stepTimerMs = 0;
            int pressedNum = 0;
        } delayData;

        // resetWheel 延迟 re-trigger
        NativeTimer resetRepressTimer;
        bool waitingForResetRepress = false;
    } m_state;
};

#endif // STEERWHEELHANDLER_H
