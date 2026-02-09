#ifndef INPUT_DISPATCHER_H
#define INPUT_DISPATCHER_H

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QPointF>
#include <QHash>
#include <atomic>

#include "keymap.h"
#include "input.h"
#include "keycodes.h"

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QTimerEvent;

class Controller;
class HandlerChain;
class SteerWheelHandler;
class ViewportHandler;
class FreeLookHandler;
class CursorHandler;
class KeyboardHandler;
class ScriptBridge;

/**
 * @brief 输入事件分发器 / Input Event Dispatcher
 *
 * 负责 / Responsible for：
 * - 处理鼠标/键盘/滚轮事件 / Handling mouse/keyboard/wheel events
 * - 管理光标状态（捕获/释放）/ Managing cursor state (capture/release)
 * - 事件分发到 HandlerChain / Dispatching events to HandlerChain
 * - 处理特定类型的键位映射 / Processing specific key mapping types
 *
 * 从 SessionContext 拆分出来，专注于输入处理。
 * Split from SessionContext, focused on input processing.
 */
class InputDispatcher : public QObject
{
    Q_OBJECT
public:
    explicit InputDispatcher(QPointer<Controller> controller, KeyMap* keyMap, QObject* parent = nullptr);
    ~InputDispatcher();

    // ========== 尺寸设置 ==========

    void setFrameSize(const QSize& size) { m_frameSize = size; }
    void setShowSize(const QSize& size) { m_showSize = size; }
    void setMobileSize(const QSize& size) { m_mobileSize = size; }
    QSize frameSize() const { return m_frameSize; }
    QSize showSize() const { return m_showSize; }
    QSize mobileSize() const { return m_mobileSize; }

    // ========== Handler 设置 ==========

    void setHandlerChain(HandlerChain* chain) { m_handlerChain = chain; }
    void setSteerWheelHandler(SteerWheelHandler* handler) { m_steerWheelHandler = handler; }
    void setViewportHandler(ViewportHandler* handler) { m_viewportHandler = handler; }
    void setFreeLookHandler(FreeLookHandler* handler) { m_freeLookHandler = handler; }
    void setCursorHandler(CursorHandler* handler) { m_cursorHandler = handler; }
    void setKeyboardHandler(KeyboardHandler* handler) { m_keyboardHandler = handler; }

    // ========== ScriptBridge 设置 ==========

    void setScriptBridge(ScriptBridge* bridge) { m_scriptBridge = bridge; }

    // ========== 光标状态 ==========

    bool isCursorCaptured() const { return m_cursorCaptured.load(); }
    bool toggleCursorCaptured();
    void setCursorCaptured(bool captured);

    // ========== 事件处理 ==========

    void mouseEvent(const QMouseEvent* from, const QSize& frameSize, const QSize& showSize);
    void wheelEvent(const QWheelEvent* from, const QSize& frameSize, const QSize& showSize);
    void keyEvent(const QKeyEvent* from, const QSize& frameSize, const QSize& showSize);

    // ========== 窗口焦点 ==========

    void onWindowFocusLost();

    // ========== 按键状态访问 ==========

    const QHash<int, bool>& keyStates() const { return m_keyStates; }

    // ========== 坐标转换 ==========

    QPointF calcFrameAbsolutePos(QPointF relativePos) const;
    QPointF calcScreenAbsolutePos(QPointF relativePos) const;

signals:
    void grabCursor(bool grab);

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    void updateSize(const QSize& frameSize, const QSize& showSize);

    // 鼠标处理
    bool processMouseClick(const QMouseEvent* from);
    bool processMouseMove(const QMouseEvent* from);
    void processCursorMouse(const QMouseEvent* from);
    void moveCursorTo(const QMouseEvent* from, const QPoint& localPosPixel);
    void mouseMoveStartTouch(const QMouseEvent* from);
    void mouseMoveStopTouch();
    void startMouseMoveTimer();
    void stopMouseMoveTimer();

    // 按键处理
    void processScript(const KeyMap::KeyMapNode& node, bool isPress);
    void processFreeLook(const KeyMap::KeyMapNode& node, const QKeyEvent* from);
    void processAndroidKey(AndroidKeycode androidKey, const QKeyEvent* from);

    // 按键转换
    void sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode);
    AndroidKeycode convertKeyCode(int key, Qt::KeyboardModifiers modifiers);

    // 触摸 ID 管理
    int attachTouchID(int key);
    void detachTouchID(int key);
    int getTouchID(int key);

private:
    QPointer<Controller> m_controller;
    KeyMap* m_keyMap = nullptr;

    QSize m_frameSize;
    QSize m_showSize;
    QSize m_mobileSize;

    // Handler 引用
    HandlerChain* m_handlerChain = nullptr;
    SteerWheelHandler* m_steerWheelHandler = nullptr;
    ViewportHandler* m_viewportHandler = nullptr;
    FreeLookHandler* m_freeLookHandler = nullptr;
    CursorHandler* m_cursorHandler = nullptr;
    KeyboardHandler* m_keyboardHandler = nullptr;

    // ScriptBridge 引用
    ScriptBridge* m_scriptBridge = nullptr;

    // 光标状态
    std::atomic<bool> m_cursorCaptured{false};
    bool m_needBackMouseMove = false;
    bool m_processMouseMove = true;

    // 多点触控 ID
    static const int MULTI_TOUCH_MAX_NUM = 10;
    int m_multiTouchID[MULTI_TOUCH_MAX_NUM] = { 0 };

    // 鼠标移动防抖
    struct {
        int timer = 0;
        int ignoreCount = 0;
    } m_ctrlMouseMove;

    // 按键状态
    QHash<int, bool> m_keyStates;
    bool m_modifierComboDetected = false;
    int m_lastModifierKey = 0;
};

#endif // INPUT_DISPATCHER_H
