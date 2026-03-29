#define _USE_MATH_DEFINES
#include <cmath>
#define LOG_TAG "InputDispatch"
#include "Logger.h"

#include <QCursor>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ThreadDispatcher.h"

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
static Size getTargetSize(const Size& frameSize, const Size& showSize, const Size& mobileSize) {
    if (mobileSize.isValid()) {
        Size target = mobileSize;
        Size refSize = frameSize.isValid() ? frameSize : showSize;
        if (refSize.isValid()) {
            bool refLandscape = refSize.width > refSize.height;
            bool mobileLandscape = target.width > target.height;
            if (refLandscape != mobileLandscape) {
                target = target.transposed();
            }
        }
        return target;
    }
    return frameSize;
}

InputDispatcher::InputDispatcher(Controller* controller, KeyMap* keyMap)
    : m_controller(controller)
    , m_keyMap(keyMap)
{
    m_ctrlMouseMove.timer.setSingleShot(true);
    m_ctrlMouseMove.timer.setInterval(500);
    m_ctrlMouseMove.timer.setCallback([this]() {
        mouseMoveStopTouch();
    });
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
            ShowCursor(FALSE);
            m_cursorHidden = true;
#else
            SetCursor(LoadCursor(NULL, IDC_CROSS));
#endif
            grabCursor.fire(true);
            if (m_viewportHandler) {
                m_viewportHandler->resetView();
            }
        }
        m_ctrlMouseMove.ignoreCount = 1;
    } else {
        if (m_cursorHidden) {
            ShowCursor(TRUE);
            m_cursorHidden = false;
        }
        grabCursor.fire(false);

        stopMouseMoveTimer();
        mouseMoveStopTouch();

        // 退出游戏模式时释放鼠标脚本触摸，防止模式切换导致触摸残留
        if (!m_pressedScriptMouseButtons.empty() && m_keyMap) {
            for (uint32_t btn : m_pressedScriptMouseButtons) {
                const KeyMap::KeyMapNode &node = m_keyMap->getKeyMapNodeMouse(static_cast<int>(btn));
                if (node.type == KeyMap::KMT_SCRIPT) {
                    processScript(node, false);
                }
            }
            m_pressedScriptMouseButtons.clear();
        }
    }
}

// ========== 事件处理 ==========

void InputDispatcher::mouseEvent(const InputEvent& from, const Size& frameSize, const Size& showSize)
{
    updateSize(frameSize, showSize);

    if (from.type == InputEventType::MousePress) {
        m_keyStates[static_cast<int>(from.button)] = true;
    } else if (from.type == InputEventType::MouseRelease) {
        m_keyStates[static_cast<int>(from.button)] = false;
    }

    // 检测"模式切换"热键
    if (m_keyMap && m_keyMap->isSwitchOnKeyboard() == false &&
        m_keyMap->getSwitchKey() == static_cast<int>(from.button)) {
        if (from.type != InputEventType::MousePress) {
            return;
        }        // 只有配置了视角控制（鼠标移动映射）时才允许切换捕获模式
        if (!m_keyMap->isValidMouseMoveMap()) {
            return;
        }        if (!toggleCursorCaptured()) {
            m_needBackMouseMove = false;
        }
        return;
    }
    // 跨模式释放：光标模式下释放游戏模式按下的鼠标脚本键
    if (from.type == InputEventType::MouseRelease &&
        !m_cursorCaptured &&
        m_pressedScriptMouseButtons.count(from.button))
    {
        m_pressedScriptMouseButtons.erase(from.button);
        if (m_keyMap) {
            const KeyMap::KeyMapNode &node = m_keyMap->getKeyMapNodeMouse(static_cast<int>(from.button));
            if (node.type == KeyMap::KMT_SCRIPT) {
                processScript(node, false);
            }
        }
    }
    // 状态分支
    if (!m_cursorCaptured) {
        // [状态 A：光标显示]
        processCursorMouse(from);
        return;
    }

    // [状态 B：光标隐藏/捕获] (游戏模式)
    if (!m_needBackMouseMove) {
        if (from.type == InputEventType::MousePress || from.type == InputEventType::MouseRelease) {
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

void InputDispatcher::wheelEvent(const InputEvent& from, const Size& frameSize, const Size& showSize)
{
    (void)frameSize;
    (void)showSize;

    if (!m_keyMap) return;

    int wheelKey = (from.wheelDelta > 0) ? WHEEL_UP : WHEEL_DOWN;

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

void InputDispatcher::keyEvent(const InputEvent& from, const Size& frameSize, const Size& showSize)
{
    if (!m_keyMap) return;

    int key = from.key;
    bool isModifier = (key == GameKey::Key_Alt || key == GameKey::Key_Shift ||
                       key == GameKey::Key_Control || key == GameKey::Key_Meta);

    if (from.type == InputEventType::KeyPress) {
        m_keyStates[key] = true;

        if (isModifier) {
            m_lastModifierKey = key;
            m_modifierComboDetected = false;
        } else if (m_lastModifierKey != 0) {
            auto it = m_keyStates.find(m_lastModifierKey);
            if (it != m_keyStates.end() && it->second) {
                m_modifierComboDetected = true;
            }
        }
    } else if (from.type == InputEventType::KeyRelease && !from.isAutoRepeat) {
        m_keyStates[key] = false;
    }

    // 检测键盘上的切换键
    if (m_keyMap->isSwitchOnKeyboard() && m_keyMap->getSwitchKey() == from.key) {
        if (InputEventType::KeyPress != from.type) {
            return;
        }
        // 只有配置了视角控制（鼠标移动映射）时才允许切换捕获模式
        if (!m_keyMap->isValidMouseMoveMap()) {
            return;
        }
        if (!toggleCursorCaptured()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    // 特殊处理：Shift+Tab -> Tab
    if (key == GameKey::Key_Backtab) {
        key = GameKey::Key_Tab;
    }

    // 获取当前按键的修饰键状态
    uint32_t mods = 0;
    if (!isModifier) {
        mods = from.modifiers & (InputModifier::Shift | InputModifier::Ctrl | InputModifier::Alt);
    }

    // 优先尝试精确匹配（带修饰键）
    const KeyMap::KeyMapNode *pNode = &m_keyMap->getKeyMapNodeKey(key, static_cast<uint32_t>(mods));

    // 辅助按键处理（Shift+数字 -> 符号键的映射）
    if (pNode->type == KeyMap::KMT_INVALID && (from.modifiers & InputModifier::Shift)) {
        int tempKey = 0;
        switch (key) {
        case GameKey::Key_Exclam:        tempKey = GameKey::Key_1; break;
        case GameKey::Key_At:            tempKey = GameKey::Key_2; break;
        case GameKey::Key_NumberSign:    tempKey = GameKey::Key_3; break;
        case GameKey::Key_Dollar:        tempKey = GameKey::Key_4; break;
        case GameKey::Key_Percent:       tempKey = GameKey::Key_5; break;
        case GameKey::Key_AsciiCircum:   tempKey = GameKey::Key_6; break;
        case GameKey::Key_Ampersand:     tempKey = GameKey::Key_7; break;
        case GameKey::Key_Asterisk:      tempKey = GameKey::Key_8; break;
        case GameKey::Key_ParenLeft:     tempKey = GameKey::Key_9; break;
        case GameKey::Key_ParenRight:    tempKey = GameKey::Key_0; break;
        case GameKey::Key_Underscore:    tempKey = GameKey::Key_Minus; break;
        case GameKey::Key_Plus:          tempKey = GameKey::Key_Equal; break;
        }

        if (tempKey != 0) {
            const KeyMap::KeyMapNode *tempNode = &m_keyMap->getKeyMapNodeKey(tempKey, static_cast<uint32_t>(mods));
            if (tempNode->type != KeyMap::KMT_INVALID) {
                pNode = tempNode;
            }
        }
    }

    updateSize(frameSize, showSize);
    if (from.isAutoRepeat) {
        return;
    }

    const KeyMap::KeyMapNode &node = *pNode;

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
        if (from.type == InputEventType::KeyPress || from.type == InputEventType::KeyRelease) {
            processScript(node, from.type == InputEventType::KeyPress);
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

    // 焦点丢失时释放所有脚本触摸点，避免按键释放事件丢失导致触摸残留
    if (m_scriptBridge) {
        m_scriptBridge->releaseAllScriptTouches();
    }
    m_pressedScriptMouseButtons.clear();

    // 清除按键状态
    m_keyStates.clear();
    m_modifierComboDetected = false;
    m_lastModifierKey = 0;
}

// ========== 内部处理函数 ==========

void InputDispatcher::updateSize(const Size& frameSize, const Size& showSize)
{
    if (showSize != m_showSize) {
        if (m_cursorCaptured && m_keyMap && m_keyMap->isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            grabCursor.fire(true);
#endif
        }
    }
    m_frameSize = frameSize;
    m_showSize = showSize;

    if (m_scriptBridge) {
        Size realSize = getTargetSize(frameSize, showSize, m_mobileSize);
        m_scriptBridge->setVideoSize(realSize);
    }
}

void InputDispatcher::processCursorMouse(const InputEvent& from)
{
    if (m_cursorHandler) {
        m_cursorHandler->processMouseEvent(from, m_showSize);
    }
}

void InputDispatcher::processScript(const KeyMap::KeyMapNode& node, bool isPress)
{
    if (!m_scriptBridge) return;

    int key = node.data.script.keyNode.key;
    const std::string& script = node.script;
    if (script.empty()) return;

    m_scriptBridge->runInlineScript(script, key, node.data.script.keyNode.pos, isPress);
}

void InputDispatcher::processFreeLook(const KeyMap::KeyMapNode& node, const InputEvent& from)
{
    if (m_freeLookHandler) {
        m_freeLookHandler->setModifierComboDetected(m_modifierComboDetected);
        m_freeLookHandler->processKeyEvent(node, from, m_frameSize, m_showSize);
    }
}

void InputDispatcher::processAndroidKey(AndroidKeycode androidKey, const InputEvent& from)
{
    if (m_keyboardHandler) {
        m_keyboardHandler->processAndroidKey(androidKey, from);
    }
}

bool InputDispatcher::processMouseClick(const InputEvent& from)
{
    if (!m_keyMap) return false;

    const KeyMap::KeyMapNode &node = m_keyMap->getKeyMapNodeMouse(static_cast<int>(from.button));
    if (KeyMap::KMT_INVALID == node.type) {
        return false;
    }

    if (node.type == KeyMap::KMT_SCRIPT) {
        if (from.type == InputEventType::MousePress) {
            m_pressedScriptMouseButtons.insert(from.button);
            processScript(node, true);
        } else if (from.type == InputEventType::MouseRelease) {
            m_pressedScriptMouseButtons.erase(from.button);
            processScript(node, false);
        }
        return true;
    }

    if (node.type == KeyMap::KMT_FREE_LOOK) {
        // 将鼠标按键事件转换为等效的键盘事件传递给 FreeLook 处理
        InputEvent keyEv = from;
        keyEv.type = (from.type == InputEventType::MousePress) ? InputEventType::KeyPress : InputEventType::KeyRelease;
        keyEv.key = node.data.freeLook.keyNode.key;
        processFreeLook(node, keyEv);
        return true;
    }

    return false;
}

bool InputDispatcher::processMouseMove(const InputEvent& from)
{
    if (InputEventType::MouseMove != from.type) {
        return false;
    }

    if (m_ctrlMouseMove.ignoreCount > 0) {
        --m_ctrlMouseMove.ignoreCount;
        return true;
    }

    int centerX = m_showSize.width / 2;
    int centerY = m_showSize.height / 2;

    PointF currentPos(from.localX, from.localY);

    PointF delta(currentPos.x - centerX, currentPos.y - centerY);
    if (delta.isNull()) {
        return true;
    }

    // Qt 6 报告逻辑坐标（DPI 缩放后），但我们需要物理鼠标位移量。
    // 乘以 DPR 将逻辑像素转换为物理像素，确保不同 DPI 缩放下视角灵敏度一致。
    if (m_devicePixelRatio > 1.0) {
        delta.x *= m_devicePixelRatio;
        delta.y *= m_devicePixelRatio;
    }

    if ((std::abs(delta.x) + std::abs(delta.y)) < 1.0) {
        return true;
    }

    m_ctrlMouseMove.ignoreCount = 1;
    moveCursorTo(from, centerX, centerY);

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

        PointF speedRatio = m_keyMap->getMouseMoveMap().data.mouseMove.speedRatio;
        Size targetSize = getTargetSize(m_frameSize, m_showSize, m_mobileSize);
        PointF distance(0, 0);

        if (targetSize.width > 0 && targetSize.height > 0 && speedRatio.x > 0 && speedRatio.y > 0) {
            distance.x = delta.x / speedRatio.x / targetSize.width;
            distance.y = delta.y / speedRatio.y / targetSize.height;
        }

        m_viewportHandler->addMoveDelta(distance);
        m_viewportHandler->scheduleMoveSend();
    }

    return true;
}

void InputDispatcher::moveCursorTo(const InputEvent& from, int localX, int localY)
{
    int posOffsetX = static_cast<int>(std::lround(from.localX)) - localX;
    int posOffsetY = static_cast<int>(std::lround(from.localY)) - localY;

    int globalX = static_cast<int>(std::lround(from.globalX)) - posOffsetX;
    int globalY = static_cast<int>(std::lround(from.globalY)) - posOffsetY;

    // QCursor::setPos 使用 Qt 逻辑坐标（与 globalPosition() 相同坐标系），
    // Qt 内部自动处理 DPR 缩放转换为物理像素。
    QCursor::setPos(globalX, globalY);
}

void InputDispatcher::mouseMoveStartTouch(const InputEvent& from)
{
    (void)from;
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
    m_ctrlMouseMove.timer.start();
}

void InputDispatcher::stopMouseMoveTimer()
{
    m_ctrlMouseMove.timer.stop();
}

// ========== 工具函数 ==========

PointF InputDispatcher::calcFrameAbsolutePos(PointF relativePos) const
{
    PointF absolutePos;
    Size targetSize = getTargetSize(m_frameSize, m_showSize, m_mobileSize);

    absolutePos.x = targetSize.width * relativePos.x;
    absolutePos.y = targetSize.height * relativePos.y;
    return absolutePos;
}

PointF InputDispatcher::calcScreenAbsolutePos(PointF relativePos) const
{
    PointF absolutePos;
    absolutePos.x = m_showSize.width * relativePos.x;
    absolutePos.y = m_showSize.height * relativePos.y;
    return absolutePos;
}

void InputDispatcher::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode)
{
    if (!m_controller) return;

    m_controller->postFastMsg(FastMsg::serializeKey(
        FastKeyEvent(action == AKEY_EVENT_ACTION_DOWN ? FKA_DOWN : FKA_UP,
                     static_cast<uint16_t>(keyCode))));
}

AndroidKeycode InputDispatcher::convertKeyCode(int key, uint32_t modifiers)
{
    AndroidKeycode keyCode = AKEYCODE_UNKNOWN;
    switch (key) {
    case GameKey::Key_Return:        keyCode = AKEYCODE_ENTER; break;
    case GameKey::Key_Enter:         keyCode = AKEYCODE_NUMPAD_ENTER; break;
    case GameKey::Key_Escape:        keyCode = AKEYCODE_ESCAPE; break;
    case GameKey::Key_Backspace:     keyCode = AKEYCODE_DEL; break;
    case GameKey::Key_Delete:        keyCode = AKEYCODE_FORWARD_DEL; break;
    case GameKey::Key_Tab:           keyCode = AKEYCODE_TAB; break;
    case GameKey::Key_Home:          keyCode = AKEYCODE_MOVE_HOME; break;
    case GameKey::Key_End:           keyCode = AKEYCODE_MOVE_END; break;
    case GameKey::Key_PageUp:        keyCode = AKEYCODE_PAGE_UP; break;
    case GameKey::Key_PageDown:      keyCode = AKEYCODE_PAGE_DOWN; break;
    case GameKey::Key_Left:          keyCode = AKEYCODE_DPAD_LEFT; break;
    case GameKey::Key_Right:         keyCode = AKEYCODE_DPAD_RIGHT; break;
    case GameKey::Key_Up:            keyCode = AKEYCODE_DPAD_UP; break;
    case GameKey::Key_Down:          keyCode = AKEYCODE_DPAD_DOWN; break;
    }

    if (AKEYCODE_UNKNOWN != keyCode) return keyCode;
    if (modifiers & (InputModifier::Alt | InputModifier::Meta)) return keyCode;

    switch (key) {
    case GameKey::Key_A: keyCode = AKEYCODE_A; break;
    case GameKey::Key_B: keyCode = AKEYCODE_B; break;
    case GameKey::Key_C: keyCode = AKEYCODE_C; break;
    case GameKey::Key_D: keyCode = AKEYCODE_D; break;
    case GameKey::Key_E: keyCode = AKEYCODE_E; break;
    case GameKey::Key_F: keyCode = AKEYCODE_F; break;
    case GameKey::Key_G: keyCode = AKEYCODE_G; break;
    case GameKey::Key_H: keyCode = AKEYCODE_H; break;
    case GameKey::Key_I: keyCode = AKEYCODE_I; break;
    case GameKey::Key_J: keyCode = AKEYCODE_J; break;
    case GameKey::Key_K: keyCode = AKEYCODE_K; break;
    case GameKey::Key_L: keyCode = AKEYCODE_L; break;
    case GameKey::Key_M: keyCode = AKEYCODE_M; break;
    case GameKey::Key_N: keyCode = AKEYCODE_N; break;
    case GameKey::Key_O: keyCode = AKEYCODE_O; break;
    case GameKey::Key_P: keyCode = AKEYCODE_P; break;
    case GameKey::Key_Q: keyCode = AKEYCODE_Q; break;
    case GameKey::Key_R: keyCode = AKEYCODE_R; break;
    case GameKey::Key_S: keyCode = AKEYCODE_S; break;
    case GameKey::Key_T: keyCode = AKEYCODE_T; break;
    case GameKey::Key_U: keyCode = AKEYCODE_U; break;
    case GameKey::Key_V: keyCode = AKEYCODE_V; break;
    case GameKey::Key_W: keyCode = AKEYCODE_W; break;
    case GameKey::Key_X: keyCode = AKEYCODE_X; break;
    case GameKey::Key_Y: keyCode = AKEYCODE_Y; break;
    case GameKey::Key_Z: keyCode = AKEYCODE_Z; break;
    case GameKey::Key_0: keyCode = AKEYCODE_0; break;
    case GameKey::Key_1: case GameKey::Key_Exclam: keyCode = AKEYCODE_1; break;
    case GameKey::Key_2: keyCode = AKEYCODE_2; break;
    case GameKey::Key_3: keyCode = AKEYCODE_3; break;
    case GameKey::Key_4: case GameKey::Key_Dollar: keyCode = AKEYCODE_4; break;
    case GameKey::Key_5: case GameKey::Key_Percent: keyCode = AKEYCODE_5; break;
    case GameKey::Key_6: case GameKey::Key_AsciiCircum: keyCode = AKEYCODE_6; break;
    case GameKey::Key_7: case GameKey::Key_Ampersand: keyCode = AKEYCODE_7; break;
    case GameKey::Key_8: keyCode = AKEYCODE_8; break;
    case GameKey::Key_9: keyCode = AKEYCODE_9; break;
    case GameKey::Key_Space: keyCode = AKEYCODE_SPACE; break;
    case GameKey::Key_Comma: case GameKey::Key_Less: keyCode = AKEYCODE_COMMA; break;
    case GameKey::Key_Period: case GameKey::Key_Greater: keyCode = AKEYCODE_PERIOD; break;
    case GameKey::Key_Minus: case GameKey::Key_Underscore: keyCode = AKEYCODE_MINUS; break;
    case GameKey::Key_Equal: keyCode = AKEYCODE_EQUALS; break;
    case GameKey::Key_BracketLeft: case GameKey::Key_BraceLeft: keyCode = AKEYCODE_LEFT_BRACKET; break;
    case GameKey::Key_BracketRight: case GameKey::Key_BraceRight: keyCode = AKEYCODE_RIGHT_BRACKET; break;
    case GameKey::Key_Backslash: case GameKey::Key_Bar: keyCode = AKEYCODE_BACKSLASH; break;
    case GameKey::Key_Semicolon: case GameKey::Key_Colon: keyCode = AKEYCODE_SEMICOLON; break;
    case GameKey::Key_Apostrophe: case GameKey::Key_QuoteDbl: keyCode = AKEYCODE_APOSTROPHE; break;
    case GameKey::Key_Slash: case GameKey::Key_Question: keyCode = AKEYCODE_SLASH; break;
    case GameKey::Key_At: keyCode = AKEYCODE_AT; break;
    case GameKey::Key_Plus: keyCode = AKEYCODE_PLUS; break;
    case GameKey::Key_QuoteLeft: case GameKey::Key_AsciiTilde: keyCode = AKEYCODE_GRAVE; break;
    case GameKey::Key_NumberSign: keyCode = AKEYCODE_POUND; break;
    case GameKey::Key_ParenLeft: keyCode = AKEYCODE_NUMPAD_LEFT_PAREN; break;
    case GameKey::Key_ParenRight: keyCode = AKEYCODE_NUMPAD_RIGHT_PAREN; break;
    case GameKey::Key_Asterisk: keyCode = AKEYCODE_STAR; break;
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
