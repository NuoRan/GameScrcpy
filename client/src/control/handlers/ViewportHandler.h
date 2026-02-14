#ifndef VIEWPORTHANDLER_H
#define VIEWPORTHANDLER_H

#include "IInputHandler.h"
#include <QTimer>
#include <QPointF>
#include <QSize>
#include <QMouseEvent>


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
    Q_OBJECT
public:
    explicit ViewportHandler(QObject* parent = nullptr);
    ~ViewportHandler();

    void init(Controller* controller, SessionContext* context) override;

    // IInputHandler interface - 视角控制不通过责任链处理，返回 false
    bool handleKeyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize) override;
    bool handleMouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize) override;
    bool handleWheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize) override;
    void onFocusLost() override;
    void reset() override;
    int priority() const override { return 80; }
    QString name() const override { return QStringLiteral("ViewportHandler"); }

    // 设置 KeyMap 引用
    void setKeyMap(KeyMap* keyMap) { m_keyMap = keyMap; }

    // ========== 视角控制核心接口（由 SessionContext 调用）==========

    // 开始/停止触摸
    void startTouch(const QSize& frameSize, const QSize& showSize);
    void stopTouch();
    bool isTouching() const { return m_state.touching; }

    // 处理移动增量（由 SessionContext::processMouseMove 调用）
    void addMoveDelta(const QPointF& delta);
    void scheduleMoveSend();

    // 供 FreeLook 检查是否正在等待回中
    bool isWaitingForCenterRepress() const { return m_state.waitingForCenterRepress; }

    // 累积待处理的增量到 pendingOvershoot（边缘回中等待期间）
    void accumulatePendingOvershoot(const QPointF& delta);

    // 获取当前触摸位置
    QPointF lastConvertedPos() const { return m_state.lastConverPos; }

    // 重置视角到中心（脚本调用）
    void resetView();

private slots:
    void onMouseMoveTimer();
    void onCenterRepressTimer();
    void onIdleCenterTimer();

private:
    void sendFastTouch(quint8 action, const QPointF& pos);
    void processMove(const QPointF& delta);

    // 注意：m_controller 和 m_sessionContext 继承自 IInputHandler
    KeyMap* m_keyMap = nullptr;

    QSize m_frameSize;
    QSize m_showSize;

    // 视角控制状态
    struct {
        QPointF lastConverPos;
        QPointF lastPos = {0.0, 0.0};
        bool touching = false;
        quint32 fastTouchSeqId = 0;

        // 边缘回中延迟状态
        bool waitingForCenterRepress = false;
        QPointF pendingCenterPos;
        QPointF pendingOvershoot;
        QTimer* centerRepressTimer = nullptr;

        // 空闲回中定时器
        QTimer* idleCenterTimer = nullptr;
        bool idleCenterCompleted = false;  // 空闲回正已完成，等待鼠标移动
    } m_state;

    // 鼠标移动发送
    QPointF m_pendingMoveDelta;
    bool m_moveSendScheduled = false;

    // ========== 游戏级视角平滑控制 ==========
    // 设计原则：原始灵敏度完全不变，只在此基础上叠加平滑和加速
    // 参考 Valorant/PUBG 的 FPS 视角控制机制：
    //  - 速度倍增器：正常移动 1:1，快速甚者加速
    //  - EMA 平滑：消除微抖动，保持丝滑
    //  - 亚像素累积：微小位移不丢失

    // 加速参数（基于速度倍增器，不改变基础灵敏度）
    static constexpr double ACCEL_LOW_THRESHOLD = 0.008;   // 低于此速度 = 1:1 线性（精确瞄准区）
    static constexpr double ACCEL_HIGH_THRESHOLD = 0.04;   // 高于此速度 = 最大倍率
    static constexpr double ACCEL_MAX_MULTIPLIER = 1.6;    // 快速甞者时的最大加速倍率
    static constexpr double ACCEL_CURVE = 2.0;             // 加速过渡曲线 (二次方平滑过渡)

    // 平滑参数
    static constexpr double SMOOTH_FACTOR = 0.85;          // EMA 平滑系数 (0.85=轻微平滑, 1.0=无平滑)
    static constexpr double JITTER_THRESHOLD = 0.00008;    // 抖动过滤阈值（极小，仅过滤传感器噪声）

    QPointF m_smoothedDelta = {0, 0};   // EMA 平滑状态
    QPointF m_subPixelAccum = {0, 0};   // 亚像素精度累积
};

#endif // VIEWPORTHANDLER_H
