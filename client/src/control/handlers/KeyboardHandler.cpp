#include "KeyboardHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "fastmsg.h"
#include <QDebug>

KeyboardHandler::KeyboardHandler(QObject* parent)
    : IInputHandler(parent)
{
}

KeyboardHandler::~KeyboardHandler()
{
}

void KeyboardHandler::init(Controller* controller, SessionContext* context)
{
    IInputHandler::init(controller, context);
}

bool KeyboardHandler::handleKeyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)

    // 不在责任链中处理键盘事件
    // KeyboardHandler 只作为工具类，由 SessionContext 直接调用 processAndroidKey/processDefaultKey
    return false;
}

bool KeyboardHandler::handleMouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
    return false;
}

bool KeyboardHandler::handleWheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize)
{
    Q_UNUSED(event)
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
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

void KeyboardHandler::processAndroidKey(AndroidKeycode androidKey, const QKeyEvent* event)
{
    if (AKEYCODE_UNKNOWN == androidKey) {
        return;
    }

    AndroidKeyeventAction action;
    switch (event->type()) {
    case QEvent::KeyPress:
        action = AKEY_EVENT_ACTION_DOWN;
        break;
    case QEvent::KeyRelease:
        action = AKEY_EVENT_ACTION_UP;
        break;
    default:
        return;
    }

    sendKeyEvent(action, androidKey);
}

void KeyboardHandler::processDefaultKey(const QKeyEvent* event)
{
    if (!event) return;

    AndroidKeyeventAction action = (event->type() == QEvent::KeyPress)
        ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;

    AndroidKeycode keyCode = convertKeyCode(event->key(), event->modifiers());
    if (keyCode != AKEYCODE_UNKNOWN) {
        sendKeyEvent(action, keyCode);
    }
}

void KeyboardHandler::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode)
{
    if (!m_controller) return;

    // [零分配优化] 栈缓冲区序列化，避免 QByteArray 堆分配
    char buf[4];
    quint8 fastAction = (action == AKEY_EVENT_ACTION_DOWN) ? FKA_DOWN : FKA_UP;
    int len = FastMsg::serializeKeyInto(buf, FastKeyEvent(fastAction, static_cast<quint16>(keyCode)));
    m_controller->postFastMsg(buf, len);
}

AndroidKeycode KeyboardHandler::convertKeyCode(int key, Qt::KeyboardModifiers modifiers)
{
    AndroidKeycode keyCode = AKEYCODE_UNKNOWN;

    // 首先处理功能键（不受修饰键影响）
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

    // Alt 或 Meta 修饰键时不处理字母数字键
    if (modifiers & (Qt::AltModifier | Qt::MetaModifier)) return keyCode;

    // 处理字母和数字键
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
