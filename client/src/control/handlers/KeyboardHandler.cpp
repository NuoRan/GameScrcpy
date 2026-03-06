#include "KeyboardHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "fastmsg.h"
#define LOG_TAG "KeyboardHandler"
#include "Logger.h"

KeyboardHandler::KeyboardHandler()
{
}

KeyboardHandler::~KeyboardHandler()
{
}

void KeyboardHandler::init(Controller* controller, SessionContext* context)
{
    IInputHandler::init(controller, context);
}

bool KeyboardHandler::handleKeyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event;
    (void)frameSize;
    (void)showSize;

    // 不在责任链中处理键盘事件
    // KeyboardHandler 只作为工具类，由 SessionContext 直接调用 processAndroidKey/processDefaultKey
    return false;
}

bool KeyboardHandler::handleMouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event;
    (void)frameSize;
    (void)showSize;
    return false;
}

bool KeyboardHandler::handleWheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    (void)event;
    (void)frameSize;
    (void)showSize;
    return false;
}

void KeyboardHandler::onFocusLost()
{
    // 键盘处理器不需要特殊的焦点丢失处理
}

void KeyboardHandler::reset()
{
    // 键盘处理器不需要特殊的重置处理
}

void KeyboardHandler::processAndroidKey(AndroidKeycode androidKey, const InputEvent& event)
{
    if (AKEYCODE_UNKNOWN == androidKey) {
        return;
    }

    AndroidKeyeventAction action;
    switch (event.type) {
    case InputEventType::KeyPress:
        action = AKEY_EVENT_ACTION_DOWN;
        break;
    case InputEventType::KeyRelease:
        action = AKEY_EVENT_ACTION_UP;
        break;
    default:
        return;
    }

    sendKeyEvent(action, androidKey);
}

void KeyboardHandler::processDefaultKey(const InputEvent& event)
{
    AndroidKeyeventAction action = (event.type == InputEventType::KeyPress)
        ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;

    AndroidKeycode keyCode = convertKeyCode(event.key, event.modifiers);
    if (keyCode != AKEYCODE_UNKNOWN) {
        sendKeyEvent(action, keyCode);
    }
}

void KeyboardHandler::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode)
{
    if (!m_controller) return;

    // 栈缓冲区序列化，避免堆分配
    char buf[4];
    uint8_t fastAction = (action == AKEY_EVENT_ACTION_DOWN) ? FKA_DOWN : FKA_UP;
    int len = FastMsg::serializeKeyInto(buf, FastKeyEvent(fastAction, static_cast<uint16_t>(keyCode)));
    m_controller->postFastMsg(buf, len);
}

AndroidKeycode KeyboardHandler::convertKeyCode(int key, uint32_t modifiers)
{
    AndroidKeycode keyCode = AKEYCODE_UNKNOWN;

    // 首先处理功能键（不受修饰键影响）
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

    // Alt 或 Meta 修饰键时不处理字母数字键
    if (modifiers & (InputModifier::Alt | InputModifier::Meta)) return keyCode;

    // 处理字母和数字键
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
