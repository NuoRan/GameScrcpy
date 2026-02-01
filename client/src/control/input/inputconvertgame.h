#ifndef INPUTCONVERTGAME_H
#define INPUTCONVERTGAME_H

#include <QPointF>
#include <QQueue>
#include <QHash>
#include <QJSEngine>
#include <QJSValue>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QImage>
#include <atomic>
#include <functional>

#include "inputconvertbase.h"
#include "keymap.h"

class ScriptApi;

#define MULTI_TOUCH_MAX_NUM 10

// 【修改】直接继承 InputConvertBase
class InputConvertGame : public InputConvertBase
{
    Q_OBJECT
public:
    InputConvertGame(Controller *controller);
    virtual ~InputConvertGame();

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) override;
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) override;
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) override;

    // 始终返回 true，因为现在只有这一种逻辑在运行，哪怕仅仅在模拟触摸
    virtual bool isCurrentCustomKeymap() override { return true; }

    void loadKeyMap(const QString &json);

    // [新增] 设置帧获取回调 (用于脚本图像识别)
    void setFrameGrabCallback(std::function<QImage()> callback);

    // ScriptApi 接口
    void script_resetView();
    void script_setSteerWheelOffset(double up, double down, double left, double right);
    QPointF script_getMousePos();
    void script_setGameMapMode(bool enter); // 保留接口，用于控制光标
    int script_getKeyState(int qtKey);
    QVariantMap script_getKeyPos(int qtKey);

protected:
    void updateSize(const QSize &frameSize, const QSize &showSize);

    // 触摸发送辅助
    void sendTouchDownEvent(int id, QPointF pos);
    void sendTouchMoveEvent(int id, QPointF pos);
    void sendTouchUpEvent(int id, QPointF pos);
    void sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action);

    // 按键发送辅助
    void sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode);

    // 【新增】移植自 Normal 的辅助转换函数
    AndroidKeycode convertKeyCode(int key, Qt::KeyboardModifiers modifiers);
    AndroidMetastate convertMetastate(Qt::KeyboardModifiers modifiers);
    AndroidMotioneventButtons convertMouseButtons(Qt::MouseButtons buttonState);
    AndroidMotioneventButtons convertMouseButton(Qt::MouseButton button);

    QPointF calcFrameAbsolutePos(QPointF relativePos);
    QPointF calcScreenAbsolutePos(QPointF relativePos);

    int attachTouchID(int key);
    void detachTouchID(int key);
    int getTouchID(int key);

    // 核心处理逻辑
    void processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from);
    void processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from);
    void processScript(const KeyMap::KeyMapNode &node, bool isPress);

    // 鼠标处理
    bool processMouseClick(const QMouseEvent *from); // 处理游戏映射点击
    bool processMouseMove(const QMouseEvent *from);  // 处理游戏视角移动

    // 【新增】处理光标显示时的普通点击（移植自 InputConvertNormal）
    void processCursorMouse(const QMouseEvent *from);

    // 【FastMsg】视角控制的触摸事件发送
    void sendViewFastTouch(quint8 action, const QPointF& pos);

    // 【FastMsg】轮盘的触摸事件发送
    void sendSteerWheelFastTouch(quint8 action, const QPointF& pos);

    void moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel);
    void mouseMoveStartTouch(const QMouseEvent *from);
    void mouseMoveStopTouch();
    void startMouseMoveTimer();
    void stopMouseMoveTimer();

    // 开关逻辑
    bool toggleCursorCaptured(); // 切换光标捕获状态
    void setCursorCaptured(bool captured);
    void resetSteerWheelState(); // 重置轮盘状态

    // 平滑延迟队列
    void getDelayQueue(const QPointF& start, const QPointF& end,
                       const double& distanceStep, const double& posStepconst,
                       quint32 lowestTimer, quint32 highestTimer,
                       QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer);

protected:
    void timerEvent(QTimerEvent *event) override;

private slots:
    void onSteerWheelTimer();
    void onMouseMoveTimer();
    void onCenterRepressTimer();  // 边缘回中延迟定时器回调
    void onIdleCenterTimer();     // 空闲回中定时器回调（鼠标不动就回中心）

private:
    QSize m_frameSize;
    QSize m_showSize;

    // 【核心】光标捕获状态
    // true: 光标隐藏，鼠标移动控制视角，点击触发映射
    // false: 光标显示，鼠标左键触发屏幕点击，WASD 等键盘映射依然有效
    // 【线程安全】使用 atomic 保护跨线程访问
    std::atomic<bool> m_cursorCaptured{false};

    bool m_needBackMouseMove = false;
    int m_multiTouchID[MULTI_TOUCH_MAX_NUM] = { 0 };
    KeyMap m_keyMap;

    bool m_processMouseMove = true;

    // 轮盘数据
    struct
    {
        int touchKey = Qt::Key_unknown;
        bool pressedUp = false;
        bool pressedDown = false;
        bool pressedLeft = false;
        bool pressedRight = false;
        quint32 fastTouchSeqId = 0;  // FastMsg 序列 ID
        struct {
            QPointF currentPos;
            QTimer* timer = nullptr;
            QQueue<QPointF> queuePos;
            QQueue<quint32> queueTimer;
            int pressedNum = 0;
        } delayData;
    } m_ctrlSteerWheel;

    // 鼠标移动数据
    struct
    {
        QPointF lastConverPos;
        QPointF lastPos = { 0.0, 0.0 };
        bool touching = false;
        int timer = 0;
        int ignoreCount = 0;
        quint32 fastTouchSeqId = 0;  // 视角控制的 FastMsg 序列 ID

        // 边缘回中延迟状态
        bool waitingForCenterRepress = false;  // 是否正在等待回中重按
        QPointF pendingCenterPos;              // 待按下的中心位置
        QPointF pendingOvershoot;              // 超出边界的剩余增量
        QTimer* centerRepressTimer = nullptr;  // 边缘回中延迟定时器

        // 空闲回中定时器（鼠标不动就回中心）
        QTimer* idleCenterTimer = nullptr;
    } m_ctrlMouseMove;

    QTimer* m_moveSendTimer = nullptr;
    QPointF m_pendingMoveDelta;

    QJSEngine* m_jsEngine = nullptr;
    ScriptApi* m_scriptApi = nullptr;
    QHash<int, bool> m_keyStates;

    // 【脚本预编译缓存】避免每次 evaluate() 的开销
    // key: 脚本字符串的 hash，value: 编译后的 QJSValue 函数
    QHash<QString, QJSValue> m_compiledScripts;
};

#endif // INPUTCONVERTGAME_H
