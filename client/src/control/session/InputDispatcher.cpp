#define _USE_MATH_DEFINES
#include <cmath>
#include <QDebug>
#include <QCursor>
#include <QGuiApplication>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimerEvent>

#include "InputDispatcher.h"
#include "ScriptBridge.h"
#include "controller.h"
#include "fastmsg.h"
#include "ConfigCenter.h"
#include "HandlerChain.h"
#include "SteerWheelHandler.h"
#include "ViewportHandler.h"
#include "FreeLookHandler.h"
#include "CursorHandler.h"
#include "KeyboardHandler.h"

#define CURSOR_POS_CHECK 50

// 辅助函数：获取目标尺寸
static QSize getTargetSize(const QSize& frameSize, const QSize& showSize, const QSize& mobileSize) {
    if (mobileSize.isValid()) {
        QSize target = mobileSize;
        QSize refSize = frameSize.isValid() ? frameSize : showSize;
        if (refSize.isValid()) {
            bool refLandscape = refSize.width() > refSize.height();
            bool mobileLandscape = target.width() > target.height();
            if (refLandscape != mobileLandscape) {
                target.transpose();
            }
        }
        return target;
    }
    return frameSize;
}

InputDispatcher::InputDispatcher(QPointer<Controller> controller, KeyMap* keyMap, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_keyMap(keyMap)
{
    setCursorCaptured(false);
}

InputDispatcher::~InputDispatcher()
{
    stopMouseMoveTimer();
    mouseMoveStopTouch();
}

// ========== 光标状态管理 ==========

bool InputDispatcher::toggleCursorCaptured()
{
    setCursorCaptured(!m_cursorCaptured);
    return m_cursorCaptured;
}

void InputDispatcher::setCursorCaptured(bool captured)
{
    m_cursorCaptured = captured;

    if (m_cursorCaptured) {
        if (m_keyMap && m_keyMap->isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
#else
            QGuiApplication::setOverrideCursor(QCursor(Qt::CrossCursor));
#endif
            emit grabCursor(true);
        }
        m_ctrlMouseMove.ignoreCount = 1;
    } else {
        QGuiApplication::restoreOverrideCursor();
        emit grabCursor(false);

        stopMouseMoveTimer();
        mouseMoveStopTouch();
    }
}

// ========== 事件处理 ==========

void InputDispatcher::mouseEvent(const QMouseEvent* from, const QSize& frameSize, const QSize& showSize)
{
    updateSize(frameSize, showSize);

    // 检测"模式切换"热键
    if (m_keyMap && m_keyMap->isSwitchOnKeyboard() == false &&
        m_keyMap->getSwitchKey() == static_cast<int>(from->button())) {
        if (from->type() != QEvent::MouseButtonPress) {
            return;
        }
        if (!toggleCursorCaptured()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    // 状态分支
    if (!m_cursorCaptured) {
        // [状态 A：光标显示]
        processCursorMouse(from);
        return;
    }

    // [状态 B：光标隐藏/捕获] (游戏模式)
    if (!m_needBackMouseMove) {
        if (from->type() == QEvent::MouseButtonPress || from->type() == QEvent::MouseButtonRelease) {
            if (processMouseClick(from)) {
                return;
            }
        }

        if (m_keyMap && m_keyMap->isValidMouseMoveMap()) {
            if (processMouseMove(from)) {
                return;
            }
        }
    }
}

void InputDispatcher::wheelEvent(const QWheelEvent* from, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(frameSize);
    Q_UNUSED(showSize);

    if (!m_keyMap) return;

    int wheelKey = (from->angleDelta().y() > 0) ? WHEEL_UP : WHEEL_DOWN;

    const KeyMap::KeyMapNode *pNode = &m_keyMap->getKeyMapNodeMouse(wheelKey);
    if (pNode->type == KeyMap::KMT_INVALID) {
        return;
    }

    switch (pNode->type) {
    case KeyMap::KMT_SCRIPT:
        processScript(*pNode, true);
        processScript(*pNode, false);
        break;
    default:
        break;
    }
}

void InputDispatcher::keyEvent(const QKeyEvent* from, const QSize& frameSize, const QSize& showSize)
{
    if (!m_keyMap) return;

    int key = from->key();
    bool isModifier = (key == Qt::Key_Alt || key == Qt::Key_Shift ||
                       key == Qt::Key_Control || key == Qt::Key_Meta);

    if (from->type() == QEvent::KeyPress) {
        m_keyStates[key] = true;

        if (isModifier) {
            m_lastModifierKey = key;
            m_modifierComboDetected = false;
        } else if (m_lastModifierKey != 0 && m_keyStates.value(m_lastModifierKey, false)) {
            m_modifierComboDetected = true;
        }
    } else if (from->type() == QEvent::KeyRelease && !from->isAutoRepeat()) {
        m_keyStates[key] = false;
    }

    // 检测键盘上的切换键
    if (m_keyMap->isSwitchOnKeyboard() && m_keyMap->getSwitchKey() == from->key()) {
        if (QEvent::KeyPress != from->type()) {
            return;
        }
        if (!toggleCursorCaptured()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    // 特殊处理：Shift+Tab -> Tab
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
    }

    // 获取当前按键的修饰键状态
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (!isModifier) {
        mods = from->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    }

    // 优先尝试精确匹配（带修饰键）
    const KeyMap::KeyMapNode *pNode = &m_keyMap->getKeyMapNodeKey(key, mods);

    // 辅助按键处理（Shift+数字 -> 符号键的映射）
    if (pNode->type == KeyMap::KMT_INVALID && (from->modifiers() & Qt::ShiftModifier)) {
        int tempKey = 0;
        switch (key) {
        case Qt::Key_Exclam:        tempKey = Qt::Key_1; break;
        case Qt::Key_At:            tempKey = Qt::Key_2; break;
        case Qt::Key_NumberSign:    tempKey = Qt::Key_3; break;
        case Qt::Key_Dollar:        tempKey = Qt::Key_4; break;
        case Qt::Key_Percent:       tempKey = Qt::Key_5; break;
        case Qt::Key_AsciiCircum:   tempKey = Qt::Key_6; break;
        case Qt::Key_Ampersand:     tempKey = Qt::Key_7; break;
        case Qt::Key_Asterisk:      tempKey = Qt::Key_8; break;
        case Qt::Key_ParenLeft:     tempKey = Qt::Key_9; break;
        case Qt::Key_ParenRight:    tempKey = Qt::Key_0; break;
        case Qt::Key_Underscore:    tempKey = Qt::Key_Minus; break;
        case Qt::Key_Plus:          tempKey = Qt::Key_Equal; break;
        }

        if (tempKey != 0) {
            const KeyMap::KeyMapNode *tempNode = &m_keyMap->getKeyMapNodeKey(tempKey, mods);
            if (tempNode->type != KeyMap::KMT_INVALID) {
                pNode = tempNode;
            }
        }
    }

    const KeyMap::KeyMapNode &node = *pNode;

    updateSize(frameSize, showSize);
    if (!from || from->isAutoRepeat()) {
        return;
    }

    // 使用 HandlerChain 处理事件
    if (m_handlerChain && m_handlerChain->dispatchKeyEvent(from, frameSize, showSize)) {
        return;
    }

    switch (node.type) {
    case KeyMap::KMT_STEER_WHEEL:
        return;

    case KeyMap::KMT_ANDROID_KEY:
        processAndroidKey(node.data.androidKey.keyNode.androidKey, from);
        return;

    case KeyMap::KMT_SCRIPT:
        if (from->type() == QEvent::KeyPress || from->type() == QEvent::KeyRelease) {
            processScript(node, from->type() == QEvent::KeyPress);
        }
        return;

    case KeyMap::KMT_FREE_LOOK:
        processFreeLook(node, from);
        return;

    case KeyMap::KMT_CAMERA_MOVE:
        break;

    default:
        if (m_keyboardHandler) {
            m_keyboardHandler->processDefaultKey(from);
        }
        break;
    }
}

void InputDispatcher::onWindowFocusLost()
{
    // 通知所有 Handler 重置状态
    if (m_handlerChain) {
        m_handlerChain->onFocusLost();
    }

    // 重置各个 Handler
    if (m_freeLookHandler) {
        m_freeLookHandler->reset();
    }

    if (m_viewportHandler && m_viewportHandler->isTouching()) {
        m_viewportHandler->stopTouch();
    }

    if (m_cursorHandler) {
        m_cursorHandler->reset();
    }

    // 清除按键状态
    m_keyStates.clear();
    m_modifierComboDetected = false;
    m_lastModifierKey = 0;
}

// ========== 内部处理函数 ==========

void InputDispatcher::updateSize(const QSize& frameSize, const QSize& showSize)
{
    if (showSize != m_showSize) {
        if (m_cursorCaptured && m_keyMap && m_keyMap->isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            emit grabCursor(true);
#endif
        }
    }
    m_frameSize = frameSize;
    m_showSize = showSize;

    if (m_scriptBridge) {
        QSize realSize = getTargetSize(frameSize, showSize, m_mobileSize);
        m_scriptBridge->setVideoSize(realSize);
    }
}

void InputDispatcher::processCursorMouse(const QMouseEvent* from)
{
    if (m_cursorHandler && from) {
        m_cursorHandler->processMouseEvent(from, m_showSize);
    }
}

void InputDispatcher::processScript(const KeyMap::KeyMapNode& node, bool isPress)
{
    if (!m_scriptBridge) return;

    int key = node.data.script.keyNode.key;
    QString script = node.script;
    if (script.isEmpty()) return;

    m_scriptBridge->runInlineScript(script, key, node.data.script.keyNode.pos, isPress);
}

void InputDispatcher::processFreeLook(const KeyMap::KeyMapNode& node, const QKeyEvent* from)
{
    if (m_freeLookHandler && from) {
        m_freeLookHandler->setModifierComboDetected(m_modifierComboDetected);
        m_freeLookHandler->processKeyEvent(node, from, m_frameSize, m_showSize);
    }
}

void InputDispatcher::processAndroidKey(AndroidKeycode androidKey, const QKeyEvent* from)
{
    if (m_keyboardHandler && from) {
        m_keyboardHandler->processAndroidKey(androidKey, from);
    }
}

bool InputDispatcher::processMouseClick(const QMouseEvent* from)
{
    if (!m_keyMap) return false;

    const KeyMap::KeyMapNode &node = m_keyMap->getKeyMapNodeMouse(from->button());
    if (KeyMap::KMT_INVALID == node.type) {
        return false;
    }

    if (node.type == KeyMap::KMT_SCRIPT) {
        if (from->type() == QEvent::MouseButtonPress || from->type() == QEvent::MouseButtonRelease) {
            processScript(node, from->type() == QEvent::MouseButtonPress);
        }
        return true;
    }

    return false;
}

bool InputDispatcher::processMouseMove(const QMouseEvent* from)
{
    if (QEvent::MouseMove != from->type()) {
        return false;
    }

    if (m_ctrlMouseMove.ignoreCount > 0) {
        --m_ctrlMouseMove.ignoreCount;
        return true;
    }

    QPoint center(m_showSize.width() / 2, m_showSize.height() / 2);

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPointF currentPos = from->localPos();
#else
    QPointF currentPos = from->position();
#endif

    QPointF delta = currentPos - center;
    if (delta.isNull()) {
        return true;
    }

    if (delta.manhattanLength() < 1.0) {
        return true;
    }

    m_ctrlMouseMove.ignoreCount = 1;
    moveCursorTo(from, center);

    // 小眼睛自由视角处理
    if (m_freeLookHandler && m_freeLookHandler->isActive() && m_freeLookHandler->hasTouchId()) {
        m_freeLookHandler->processMouseDelta(delta, m_frameSize, m_showSize);
        return true;
    }

    // 正常的移动逻辑
    if (m_processMouseMove && m_viewportHandler && m_keyMap) {
        if (!m_viewportHandler->isTouching() && !m_viewportHandler->isWaitingForCenterRepress()) {
            m_viewportHandler->startTouch(m_frameSize, m_showSize);
        }

        QPointF speedRatio = m_keyMap->getMouseMoveMap().data.mouseMove.speedRatio;
        QSize targetSize = getTargetSize(m_frameSize, m_showSize, m_mobileSize);
        QPointF distance(0, 0);

        if (targetSize.width() > 0 && targetSize.height() > 0 && speedRatio.x() > 0 && speedRatio.y() > 0) {
            distance.setX(delta.x() / speedRatio.x() / targetSize.width());
            distance.setY(delta.y() / speedRatio.y() / targetSize.height());
        }

        m_viewportHandler->addMoveDelta(distance);
        m_viewportHandler->scheduleMoveSend();
    }

    return true;
}

void InputDispatcher::moveCursorTo(const QMouseEvent* from, const QPoint& localPosPixel)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPoint posOffset = from->pos() - localPosPixel;
    QPoint globalPos = from->globalPos();
#else
    QPoint posOffset = from->position().toPoint() - localPosPixel;
    QPoint globalPos = from->globalPosition().toPoint();
#endif
    globalPos -= posOffset;

    QTimer::singleShot(0, [globalPos]() {
        QCursor::setPos(globalPos);
    });
}

void InputDispatcher::mouseMoveStartTouch(const QMouseEvent* from)
{
    Q_UNUSED(from)
    if (m_viewportHandler) {
        m_viewportHandler->startTouch(m_frameSize, m_showSize);
    }
}

void InputDispatcher::mouseMoveStopTouch()
{
    if (m_viewportHandler) {
        m_viewportHandler->stopTouch();
    }
}

void InputDispatcher::startMouseMoveTimer()
{
    stopMouseMoveTimer();
    m_ctrlMouseMove.timer = startTimer(500);
}

void InputDispatcher::stopMouseMoveTimer()
{
    if (0 != m_ctrlMouseMove.timer) {
        killTimer(m_ctrlMouseMove.timer);
        m_ctrlMouseMove.timer = 0;
    }
}

void InputDispatcher::timerEvent(QTimerEvent* event)
{
    if (m_ctrlMouseMove.timer == event->timerId()) {
        stopMouseMoveTimer();
        mouseMoveStopTouch();
    }
}

// ========== 工具函数 ==========

QPointF InputDispatcher::calcFrameAbsolutePos(QPointF relativePos) const
{
    QPointF absolutePos;
    QSize targetSize = getTargetSize(m_frameSize, m_showSize, m_mobileSize);

    absolutePos.setX(targetSize.width() * relativePos.x());
    absolutePos.setY(targetSize.height() * relativePos.y());
    return absolutePos;
}

QPointF InputDispatcher::calcScreenAbsolutePos(QPointF relativePos) const
{
    QPointF absolutePos;
    absolutePos.setX(m_showSize.width() * relativePos.x());
    absolutePos.setY(m_showSize.height() * relativePos.y());
    return absolutePos;
}

void InputDispatcher::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode)
{
    if (m_controller.isNull()) return;

    QByteArray data;
    if (action == AKEY_EVENT_ACTION_DOWN) {
        data = FastMsg::keyDown(static_cast<quint16>(keyCode));
    } else {
        data = FastMsg::keyUp(static_cast<quint16>(keyCode));
    }

    m_controller->postFastMsg(data);
}

AndroidKeycode InputDispatcher::convertKeyCode(int key, Qt::KeyboardModifiers modifiers)
{
    AndroidKeycode keyCode = AKEYCODE_UNKNOWN;
    switch (key) {
    case Qt::Key_Return:        keyCode = AKEYCODE_ENTER; break;
    case Qt::Key_Enter:         keyCode = AKEYCODE_NUMPAD_ENTER; break;
    case Qt::Key_Escape:        keyCode = AKEYCODE_ESCAPE; break;
    case Qt::Key_Backspace:     keyCode = AKEYCODE_DEL; break;
    case Qt::Key_Delete:        keyCode = AKEYCODE_FORWARD_DEL; break;
    case Qt::Key_Tab:           keyCode = AKEYCODE_TAB; break;
    case Qt::Key_Home:          keyCode = AKEYCODE_MOVE_HOME; break;
    case Qt::Key_End:           keyCode = AKEYCODE_MOVE_END; break;
    case Qt::Key_PageUp:        keyCode = AKEYCODE_PAGE_UP; break;
    case Qt::Key_PageDown:      keyCode = AKEYCODE_PAGE_DOWN; break;
    case Qt::Key_Left:          keyCode = AKEYCODE_DPAD_LEFT; break;
    case Qt::Key_Right:         keyCode = AKEYCODE_DPAD_RIGHT; break;
    case Qt::Key_Up:            keyCode = AKEYCODE_DPAD_UP; break;
    case Qt::Key_Down:          keyCode = AKEYCODE_DPAD_DOWN; break;
    }

    if (AKEYCODE_UNKNOWN != keyCode) return keyCode;
    if (modifiers & (Qt::AltModifier | Qt::MetaModifier)) return keyCode;

    switch (key) {
    case Qt::Key_A: keyCode = AKEYCODE_A; break;
    case Qt::Key_B: keyCode = AKEYCODE_B; break;
    case Qt::Key_C: keyCode = AKEYCODE_C; break;
    case Qt::Key_D: keyCode = AKEYCODE_D; break;
    case Qt::Key_E: keyCode = AKEYCODE_E; break;
    case Qt::Key_F: keyCode = AKEYCODE_F; break;
    case Qt::Key_G: keyCode = AKEYCODE_G; break;
    case Qt::Key_H: keyCode = AKEYCODE_H; break;
    case Qt::Key_I: keyCode = AKEYCODE_I; break;
    case Qt::Key_J: keyCode = AKEYCODE_J; break;
    case Qt::Key_K: keyCode = AKEYCODE_K; break;
    case Qt::Key_L: keyCode = AKEYCODE_L; break;
    case Qt::Key_M: keyCode = AKEYCODE_M; break;
    case Qt::Key_N: keyCode = AKEYCODE_N; break;
    case Qt::Key_O: keyCode = AKEYCODE_O; break;
    case Qt::Key_P: keyCode = AKEYCODE_P; break;
    case Qt::Key_Q: keyCode = AKEYCODE_Q; break;
    case Qt::Key_R: keyCode = AKEYCODE_R; break;
    case Qt::Key_S: keyCode = AKEYCODE_S; break;
    case Qt::Key_T: keyCode = AKEYCODE_T; break;
    case Qt::Key_U: keyCode = AKEYCODE_U; break;
    case Qt::Key_V: keyCode = AKEYCODE_V; break;
    case Qt::Key_W: keyCode = AKEYCODE_W; break;
    case Qt::Key_X: keyCode = AKEYCODE_X; break;
    case Qt::Key_Y: keyCode = AKEYCODE_Y; break;
    case Qt::Key_Z: keyCode = AKEYCODE_Z; break;
    case Qt::Key_0: keyCode = AKEYCODE_0; break;
    case Qt::Key_1: case Qt::Key_Exclam: keyCode = AKEYCODE_1; break;
    case Qt::Key_2: keyCode = AKEYCODE_2; break;
    case Qt::Key_3: keyCode = AKEYCODE_3; break;
    case Qt::Key_4: case Qt::Key_Dollar: keyCode = AKEYCODE_4; break;
    case Qt::Key_5: case Qt::Key_Percent: keyCode = AKEYCODE_5; break;
    case Qt::Key_6: case Qt::Key_AsciiCircum: keyCode = AKEYCODE_6; break;
    case Qt::Key_7: case Qt::Key_Ampersand: keyCode = AKEYCODE_7; break;
    case Qt::Key_8: keyCode = AKEYCODE_8; break;
    case Qt::Key_9: keyCode = AKEYCODE_9; break;
    case Qt::Key_Space: keyCode = AKEYCODE_SPACE; break;
    case Qt::Key_Comma: case Qt::Key_Less: keyCode = AKEYCODE_COMMA; break;
    case Qt::Key_Period: case Qt::Key_Greater: keyCode = AKEYCODE_PERIOD; break;
    case Qt::Key_Minus: case Qt::Key_Underscore: keyCode = AKEYCODE_MINUS; break;
    case Qt::Key_Equal: keyCode = AKEYCODE_EQUALS; break;
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: keyCode = AKEYCODE_LEFT_BRACKET; break;
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: keyCode = AKEYCODE_RIGHT_BRACKET; break;
    case Qt::Key_Backslash: case Qt::Key_Bar: keyCode = AKEYCODE_BACKSLASH; break;
    case Qt::Key_Semicolon: case Qt::Key_Colon: keyCode = AKEYCODE_SEMICOLON; break;
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: keyCode = AKEYCODE_APOSTROPHE; break;
    case Qt::Key_Slash: case Qt::Key_Question: keyCode = AKEYCODE_SLASH; break;
    case Qt::Key_At: keyCode = AKEYCODE_AT; break;
    case Qt::Key_Plus: keyCode = AKEYCODE_PLUS; break;
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: keyCode = AKEYCODE_GRAVE; break;
    case Qt::Key_NumberSign: keyCode = AKEYCODE_POUND; break;
    case Qt::Key_ParenLeft: keyCode = AKEYCODE_NUMPAD_LEFT_PAREN; break;
    case Qt::Key_ParenRight: keyCode = AKEYCODE_NUMPAD_RIGHT_PAREN; break;
    case Qt::Key_Asterisk: keyCode = AKEYCODE_STAR; break;
    }
    return keyCode;
}

// ========== 触摸 ID 管理 ==========

int InputDispatcher::attachTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (0 == m_multiTouchID[i]) {
            m_multiTouchID[i] = key;
            return i;
        }
    }
    return -1;
}

void InputDispatcher::detachTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            m_multiTouchID[i] = 0;
            return;
        }
    }
}

int InputDispatcher::getTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            return i;
        }
    }
    return -1;
}
