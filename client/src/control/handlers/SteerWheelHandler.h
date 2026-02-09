#ifndef STEERWHEELHANDLER_H
#define STEERWHEELHANDLER_H

#include "IInputHandler.h"
#include "keymap.h"
#include <QTimer>
#include <QQueue>
#include <QPointF>

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
    Q_OBJECT
public:
    explicit SteerWheelHandler(QObject* parent = nullptr);
    ~SteerWheelHandler() override;

    void init(Controller* controller, SessionContext* context) override;
    bool handleKeyEvent(const QKeyEvent* event,
                        const QSize& frameSize,
                        const QSize& showSize) override;
    void onFocusLost() override;
    void reset() override;
    QString name() const override { return "SteerWheelHandler"; }
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

private slots:
    void onSteerWheelTimer();
    void onFirstPressTimer();
    void onHumanizeTimer();

private:
    void processSteerWheel(const KeyMap::KeyMapNode& node, const QKeyEvent* event);
    void executeMove(const KeyMap::KeyMapNode& node);
    void sendFastTouch(quint8 action, const QPointF& pos);
    void getDelayQueue(const QPointF& start, const QPointF& end,
                       double distanceStep, double posStep,
                       quint32 lowestTimer, quint32 highestTimer,
                       QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer);

    KeyMap* m_keyMap = nullptr;
    QSize m_frameSize;
    QSize m_showSize;

    // 轮盘状态
    struct {
        int touchKey = Qt::Key_unknown;
        bool pressedUp = false;
        bool pressedDown = false;
        bool pressedLeft = false;
        bool pressedRight = false;
        quint32 fastTouchSeqId = 0;
        bool isFirstPress = true;
        QTimer* firstPressTimer = nullptr;
        const KeyMap::KeyMapNode* pendingNode = nullptr;

        // 拟人化参数
        double currentAngleOffset = 0.0;
        double currentLengthFactor = 1.0;
        double targetAngleOffset = 0.0;
        double targetLengthFactor = 1.0;
        QTimer* humanizeTimer = nullptr;
        int lastPressedState = 0;

        // 延迟数据
        struct {
            QPointF currentPos;
            QTimer* timer = nullptr;
            QQueue<QPointF> queuePos;
            QQueue<quint32> queueTimer;
            int pressedNum = 0;
        } delayData;
    } m_state;
};

#endif // STEERWHEELHANDLER_H
